"""Training script for TSMixer SOH model.

Uses synthetic data + optional real InfluxDB data for training.
Supports multi-head loss: SOH (MSE) + RUL (Huber) + Anomaly (BCE).
"""

from __future__ import annotations

import argparse
import logging
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

from soh.config import settings
from soh.synthetic import generate_dataset
from soh.tsmixer import TSMixer

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)-8s %(message)s")
logger = logging.getLogger("train_tsmixer")


def train_tsmixer(
    output_dir: Path,
    n_synthetic: int = 5000,
    epochs: int = 50,
    batch_size: int = 32,
    lr: float = 1e-3,
    hidden: int = 64,
    n_mix_layers: int = 3,
    seed: int = 42,
) -> dict:
    """Train TSMixer model on synthetic + optional real data.

    Returns dict with training metrics.
    """
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    logger.info("Device: %s", device)

    # Generate synthetic training data
    logger.info("Generating %d synthetic profiles...", n_synthetic)
    X, y_soh, y_rul, y_anomaly = generate_dataset(
        n_samples=n_synthetic,
        seq_len=settings.tsmixer_seq_len,
        seed=seed,
    )

    # Normalize RUL to [0, 1] range for balanced loss
    rul_max = y_rul.max()
    if rul_max > 0:
        y_rul_norm = y_rul / rul_max
    else:
        y_rul_norm = y_rul

    # Train/val split (80/20)
    rng = np.random.default_rng(seed)
    idx = rng.permutation(len(X))
    split = int(0.8 * len(X))
    train_idx, val_idx = idx[:split], idx[split:]

    X_train = torch.from_numpy(X[train_idx])
    X_val = torch.from_numpy(X[val_idx])
    y_soh_train = torch.from_numpy(y_soh[train_idx])
    y_soh_val = torch.from_numpy(y_soh[val_idx])
    y_rul_train = torch.from_numpy(y_rul_norm[train_idx])
    y_rul_val = torch.from_numpy(y_rul_norm[val_idx])
    y_anomaly_train = torch.from_numpy(y_anomaly[train_idx])
    y_anomaly_val = torch.from_numpy(y_anomaly[val_idx])

    train_ds = TensorDataset(X_train, y_soh_train, y_rul_train, y_anomaly_train)
    val_ds = TensorDataset(X_val, y_soh_val, y_rul_val, y_anomaly_val)
    train_loader = DataLoader(train_ds, batch_size=batch_size, shuffle=True, drop_last=True)
    val_loader = DataLoader(val_ds, batch_size=batch_size * 2, shuffle=False)

    # Model
    model = TSMixer(
        n_features=settings.tsmixer_n_features,
        hidden=hidden,
        n_mix_layers=n_mix_layers,
    ).to(device)

    n_params = sum(p.numel() for p in model.parameters())
    logger.info("TSMixer: hidden=%d, layers=%d, params=%d", hidden, n_mix_layers, n_params)

    # Loss functions: weighted multi-head
    criterion_soh = nn.MSELoss()
    criterion_rul = nn.HuberLoss(delta=0.1)
    criterion_anomaly = nn.BCELoss()
    optimizer = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=epochs)

    # Loss weights
    w_soh, w_rul, w_anomaly = 1.0, 0.5, 0.3

    best_val_loss = float("inf")
    best_epoch = -1
    best_state = None
    patience = 10
    patience_counter = 0
    t0 = time.time()

    for epoch in range(1, epochs + 1):
        # Train
        model.train()
        train_loss_sum = 0.0
        train_n = 0
        for xb, ys, yr, ya in train_loader:
            xb = xb.to(device)
            ys, yr, ya = ys.to(device), yr.to(device), ya.to(device)

            soh_pred, rul_pred, anom_pred = model(xb)
            loss = (
                w_soh * criterion_soh(soh_pred, ys)
                + w_rul * criterion_rul(rul_pred, yr)
                + w_anomaly * criterion_anomaly(anom_pred, ya)
            )

            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()

            train_loss_sum += loss.item() * len(xb)
            train_n += len(xb)

        scheduler.step()
        train_loss = train_loss_sum / train_n

        # Validate
        model.eval()
        val_loss_sum = 0.0
        val_n = 0
        with torch.no_grad():
            for xb, ys, yr, ya in val_loader:
                xb = xb.to(device)
                ys, yr, ya = ys.to(device), yr.to(device), ya.to(device)

                soh_pred, rul_pred, anom_pred = model(xb)
                loss = (
                    w_soh * criterion_soh(soh_pred, ys)
                    + w_rul * criterion_rul(rul_pred, yr)
                    + w_anomaly * criterion_anomaly(anom_pred, ya)
                )
                val_loss_sum += loss.item() * len(xb)
                val_n += len(xb)

        val_loss = val_loss_sum / val_n

        if epoch == 1 or epoch % 10 == 0 or epoch == epochs:
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

    # Restore best
    if best_state:
        model.load_state_dict(best_state)
        model.to(device)

    # Save
    output_dir.mkdir(parents=True, exist_ok=True)
    pt_path = output_dir / "tsmixer_soh.pt"
    torch.save({
        "model_state_dict": model.state_dict(),
        "n_features": settings.tsmixer_n_features,
        "hidden": hidden,
        "n_mix_layers": n_mix_layers,
        "best_epoch": best_epoch,
        "best_val_loss": best_val_loss,
        "rul_max": float(rul_max),
        "n_params": n_params,
    }, pt_path)

    logger.info("Saved TSMixer: %s (%.1f KB, %d params, best epoch %d)",
                pt_path, pt_path.stat().st_size / 1024, n_params, best_epoch)

    return {
        "n_params": n_params,
        "best_epoch": best_epoch,
        "best_val_loss": float(best_val_loss),
        "rul_max": float(rul_max),
        "model_path": str(pt_path),
    }


def main():
    parser = argparse.ArgumentParser(description="Train TSMixer for battery SOH scoring")
    parser.add_argument("--output-dir", type=Path, default=Path("models"))
    parser.add_argument("--n-synthetic", type=int, default=5000)
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--hidden", type=int, default=64)
    parser.add_argument("--n-mix-layers", type=int, default=3)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    train_tsmixer(
        output_dir=args.output_dir,
        n_synthetic=args.n_synthetic,
        epochs=args.epochs,
        batch_size=args.batch_size,
        lr=args.lr,
        hidden=args.hidden,
        n_mix_layers=args.n_mix_layers,
        seed=args.seed,
    )


if __name__ == "__main__":
    main()
