"""Training script for FleetGNN model.

Uses synthetic fleet scenarios for training:
- One degraded battery among healthy ones (outlier detection)
- Gradual fleet-wide degradation (calendar aging)
- Imbalanced fleet (mixed chemistry/age/capacity)
"""

from __future__ import annotations

import argparse
import logging
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

from soh.config import settings
from soh.gnn import FleetGNN, build_fleet_graph
from soh.synthetic import DegradationMode, generate_profile

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)-8s %(message)s")
logger = logging.getLogger("train_gnn")


def generate_fleet_scenario(
    n_batteries: int,
    rng: np.random.Generator,
    tsmixer_encoder: nn.Module | None = None,
) -> dict:
    """Generate a single fleet training scenario.

    Returns dict with keys: node_feats, voltages, currents, fleet_health, outlier_idx, imbalance
    """
    scenario_type = rng.choice(["healthy", "one_degraded", "fleet_degraded", "imbalanced"],
                                p=[0.25, 0.35, 0.20, 0.20])

    profiles = []
    if scenario_type == "healthy":
        for _ in range(n_batteries):
            profiles.append(generate_profile(DegradationMode.HEALTHY, rng=rng))
        fleet_health = rng.uniform(0.85, 1.0)
        outlier_idx = 0  # no real outlier
        imbalance = rng.uniform(0.0, 0.1)

    elif scenario_type == "one_degraded":
        outlier_idx = rng.integers(0, n_batteries)
        for i in range(n_batteries):
            if i == outlier_idx:
                mode = rng.choice([DegradationMode.CYCLE, DegradationMode.SUDDEN_FAILURE])
                profiles.append(generate_profile(mode, rng=rng, severity=rng.uniform(0.5, 1.0)))
            else:
                profiles.append(generate_profile(DegradationMode.HEALTHY, rng=rng))
        fleet_health = rng.uniform(0.5, 0.85)
        imbalance = rng.uniform(0.3, 0.8)

    elif scenario_type == "fleet_degraded":
        severity = rng.uniform(0.3, 0.8)
        for _ in range(n_batteries):
            profiles.append(generate_profile(DegradationMode.CALENDAR, rng=rng, severity=severity))
        fleet_health = max(0.0, 1.0 - severity * 0.5)
        outlier_idx = 0
        imbalance = rng.uniform(0.0, 0.2)

    else:  # imbalanced
        for i in range(n_batteries):
            severity = rng.uniform(0.0, 0.8)
            mode = rng.choice(list(DegradationMode))
            profiles.append(generate_profile(mode, rng=rng, severity=severity))
        # Outlier = most degraded
        soh_scores = [p.soh_score for p in profiles]
        outlier_idx = int(np.argmin(soh_scores))
        fleet_health = float(np.mean(soh_scores))
        imbalance = float(np.std(soh_scores))

    # Build node features: use TSMixer encoder if available, else use mean features
    if tsmixer_encoder is not None:
        with torch.no_grad():
            features_batch = torch.stack([torch.from_numpy(p.features) for p in profiles])
            node_feats = tsmixer_encoder(features_batch)
    else:
        # Fallback: mean of features per battery as node embedding (padded to 64)
        node_feats_list = []
        for p in profiles:
            mean_feats = p.features.mean(axis=0)  # (17,)
            padded = np.zeros(settings.gnn_node_features, dtype=np.float32)
            padded[:len(mean_feats)] = mean_feats
            node_feats_list.append(padded)
        node_feats = torch.from_numpy(np.array(node_feats_list))

    # Extract voltages and currents for edge features
    voltages = torch.tensor([p.features[:, 5].mean() for p in profiles])
    currents = torch.tensor([p.features[:, 10].mean() for p in profiles])

    return {
        "node_feats": node_feats,
        "voltages": voltages,
        "currents": currents,
        "fleet_health": fleet_health,
        "outlier_idx": outlier_idx,
        "outlier_score": 1.0 - profiles[outlier_idx].soh_score,
        "imbalance": min(1.0, imbalance),
    }


def train_gnn(
    output_dir: Path,
    n_scenarios: int = 2000,
    epochs: int = 30,
    lr: float = 1e-3,
    hidden: int = 32,
    n_layers: int = 2,
    heads: int = 4,
    seed: int = 42,
) -> dict:
    """Train FleetGNN on synthetic fleet scenarios."""
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    rng = np.random.default_rng(seed)

    model = FleetGNN(
        node_features=settings.gnn_node_features,
        hidden=hidden,
        n_layers=n_layers,
        heads=heads,
    ).to(device)

    n_params = sum(p.numel() for p in model.parameters())
    logger.info("FleetGNN: hidden=%d, layers=%d, heads=%d, params=%d", hidden, n_layers, heads, n_params)

    optimizer = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)

    # Pre-generate scenarios
    logger.info("Generating %d fleet scenarios...", n_scenarios)
    scenarios = []
    for _ in range(n_scenarios):
        n_bat = rng.integers(2, 24)
        scenarios.append(generate_fleet_scenario(n_bat, rng))

    split = int(0.8 * len(scenarios))
    train_scenarios = scenarios[:split]
    val_scenarios = scenarios[split:]

    criterion_health = nn.MSELoss()
    criterion_outlier = nn.MSELoss()
    criterion_imbalance = nn.MSELoss()

    best_val_loss = float("inf")
    best_epoch = -1
    best_state = None
    patience = 8
    patience_counter = 0
    t0 = time.time()

    for epoch in range(1, epochs + 1):
        # Train
        model.train()
        train_loss_sum = 0.0
        rng.shuffle(train_scenarios)

        for sc in train_scenarios:
            node_feats = sc["node_feats"].to(device)
            edge_index, edge_attr = build_fleet_graph(
                node_feats, sc["voltages"].to(device), sc["currents"].to(device)
            )

            fleet_health, _, outlier_score, imbalance = model(node_feats, edge_index, edge_attr)

            target_health = torch.tensor(sc["fleet_health"], device=device, dtype=torch.float)
            target_outlier = torch.tensor(sc["outlier_score"], device=device, dtype=torch.float)
            target_imbalance = torch.tensor(sc["imbalance"], device=device, dtype=torch.float)

            loss = (
                criterion_health(fleet_health, target_health)
                + 0.5 * criterion_outlier(outlier_score, target_outlier)
                + 0.3 * criterion_imbalance(imbalance, target_imbalance)
            )

            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()

            train_loss_sum += loss.item()

        train_loss = train_loss_sum / len(train_scenarios)

        # Validate
        model.eval()
        val_loss_sum = 0.0
        with torch.no_grad():
            for sc in val_scenarios:
                node_feats = sc["node_feats"].to(device)
                edge_index, edge_attr = build_fleet_graph(
                    node_feats, sc["voltages"].to(device), sc["currents"].to(device)
                )
                fleet_health, _, outlier_score, imbalance = model(node_feats, edge_index, edge_attr)

                target_health = torch.tensor(sc["fleet_health"], device=device, dtype=torch.float)
                target_outlier = torch.tensor(sc["outlier_score"], device=device, dtype=torch.float)
                target_imbalance = torch.tensor(sc["imbalance"], device=device, dtype=torch.float)

                loss = (
                    criterion_health(fleet_health, target_health)
                    + 0.5 * criterion_outlier(outlier_score, target_outlier)
                    + 0.3 * criterion_imbalance(imbalance, target_imbalance)
                )
                val_loss_sum += loss.item()

        val_loss = val_loss_sum / len(val_scenarios)

        if epoch == 1 or epoch % 5 == 0 or epoch == epochs:
            logger.info("Epoch %3d/%d  train=%.6f  val=%.6f  (%.1fs)",
                        epoch, epochs, train_loss, val_loss, time.time() - t0)

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_epoch = epoch
            patience_counter = 0
            best_state = {k: v.cpu().clone() for k, v in model.state_dict().items()}
        else:
            patience_counter += 1
            if patience_counter >= patience:
                logger.info("Early stopping at epoch %d (best: %d)", epoch, best_epoch)
                break

    if best_state:
        model.load_state_dict(best_state)

    output_dir.mkdir(parents=True, exist_ok=True)
    pt_path = output_dir / "gnn_fleet.pt"
    torch.save({
        "model_state_dict": model.state_dict(),
        "node_features": settings.gnn_node_features,
        "hidden": hidden,
        "n_layers": n_layers,
        "heads": heads,
        "best_epoch": best_epoch,
        "best_val_loss": best_val_loss,
        "n_params": n_params,
    }, pt_path)

    logger.info("Saved FleetGNN: %s (%.1f KB)", pt_path, pt_path.stat().st_size / 1024)
    return {"n_params": n_params, "best_epoch": best_epoch, "best_val_loss": float(best_val_loss)}


def main():
    parser = argparse.ArgumentParser(description="Train FleetGNN for fleet-level scoring")
    parser.add_argument("--output-dir", type=Path, default=Path("models"))
    parser.add_argument("--n-scenarios", type=int, default=2000)
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--hidden", type=int, default=32)
    parser.add_argument("--n-layers", type=int, default=2)
    parser.add_argument("--heads", type=int, default=4)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    train_gnn(
        output_dir=args.output_dir,
        n_scenarios=args.n_scenarios,
        epochs=args.epochs,
        lr=args.lr,
        hidden=args.hidden,
        n_layers=args.n_layers,
        heads=args.heads,
        seed=args.seed,
    )


if __name__ == "__main__":
    main()
