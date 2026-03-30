#!/usr/bin/env python3
"""
train_fpnn.py — Train a Feature-based Polynomial Neural Network for battery SOH prediction.

FPNN = polynomial feature expansion + small MLP.  Designed to be tiny (~5K params)
so the model can run on edge devices (ESP32, microcontrollers via ONNX Runtime Micro).

SOH proxy
---------
Ground-truth State-of-Health requires controlled lab measurements (full charge/discharge
cycles with calibrated equipment).  This dataset has no lab SOH labels, so we construct
a *proxy* target:

    Phase 1 (broken):   soh_proxy = rest_voltage_normalised  (DEVICE-DEPENDENT, 56% NaN) ✗
    Phase 2.2 (fixed):  soh_proxy = capacity_fade_ratio      (DEVICE-INDEPENDENT) ✓

    Capacity fade is computed from ah_cons (cumulative discharge) and is physics-based,
    independent of device-specific voltage offsets. See build_soh_proxy_capacity().

    A secondary voltage-based proxy is available via --soh-mode=voltage for reference only.

Usage
-----
    python scripts/ml/train_fpnn.py --input data/features.parquet
    python scripts/ml/train_fpnn.py --input data/features.parquet --degree 2 --hidden 64 --soh-mode capacity
"""

from __future__ import annotations

import argparse
import logging
import os
import sys
import time
from pathlib import Path

import numpy as np
import pandas as pd
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("fpnn")

# ---------------------------------------------------------------------------
# Feature columns used as model inputs
# ---------------------------------------------------------------------------
FEATURE_COLS = [
    "V_mean",
    "V_std",
    "I_mean",
    "I_std",
    "dV_dt",
    "dI_dt",
    "ah_cons",
    "ah_charge",
    "V_min",
    "V_max",
    "I_max",
    "samples",
    "R_internal",
]


# ---------------------------------------------------------------------------
# FPNN model
# ---------------------------------------------------------------------------
class PolynomialExpansion(nn.Module):
    """Expand input features to include all monomials up to *degree*.

    For degree=2 and n features the output has  n + n*(n+1)/2  columns
    (original features + all pairwise products including squares).
    For degree=3 it adds the cubic terms on top.
    """

    def __init__(self, n_features: int, degree: int = 3):
        super().__init__()
        self.n_features = n_features
        self.degree = degree
        # Pre-compute index pairs / triples so forward() is just gather+prod.
        pairs = []
        for i in range(n_features):
            for j in range(i, n_features):
                pairs.append((i, j))
        self.register_buffer("pair_idx", torch.tensor(pairs, dtype=torch.long))

        triples = []
        if degree >= 3:
            for i in range(n_features):
                for j in range(i, n_features):
                    for k in range(j, n_features):
                        triples.append((i, j, k))
            self.register_buffer("triple_idx", torch.tensor(triples, dtype=torch.long))
        else:
            self.register_buffer("triple_idx", torch.zeros(0, 3, dtype=torch.long))

        self.out_features = n_features + len(pairs) + len(triples)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        parts = [x]
        # Degree-2 terms
        a = x[:, self.pair_idx[:, 0]]
        b = x[:, self.pair_idx[:, 1]]
        parts.append(a * b)
        # Degree-3 terms
        if self.degree >= 3 and self.triple_idx.shape[0] > 0:
            a3 = x[:, self.triple_idx[:, 0]]
            b3 = x[:, self.triple_idx[:, 1]]
            c3 = x[:, self.triple_idx[:, 2]]
            parts.append(a3 * b3 * c3)
        return torch.cat(parts, dim=1)


class FPNN(nn.Module):
    """Feature-based Polynomial Neural Network — tiny model for edge deployment.

    Architecture:
        input (n_features)
          -> PolynomialExpansion (degree 2 or 3)
          -> Linear(expanded, hidden) -> ReLU -> Dropout
          -> Linear(hidden, 1) -> Sigmoid
    """

    def __init__(self, n_features: int, hidden: int = 32, degree: int = 3, dropout: float = 0.1):
        super().__init__()
        self.poly = PolynomialExpansion(n_features, degree)
        self.net = nn.Sequential(
            nn.Linear(self.poly.out_features, hidden),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hidden, 1),
            nn.Sigmoid(),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.net(self.poly(x)).squeeze(-1)


# ---------------------------------------------------------------------------
# Data helpers
# ---------------------------------------------------------------------------

def build_soh_proxy_voltage(df: pd.DataFrame) -> pd.Series:
    """Normalise rest_voltage per device+channel to [0, 1]."""
    grouped = df.groupby(["device", "channel"])["rest_voltage"]
    vmin = grouped.transform("min")
    vmax = grouped.transform("max")
    span = (vmax - vmin).replace(0, 1.0)  # avoid div-by-zero
    return (df["rest_voltage"] - vmin) / span


def build_soh_proxy_capacity(df: pd.DataFrame) -> pd.Series:
    """Use cumulative ah_cons trend as capacity-fade proxy.

    For each device+channel, the maximum ah_cons observed is treated as
    nominal capacity.  SOH proxy = 1 - (ah_cons / max_ah_cons) so rows
    near full capacity show lower SOH (more faded).  This is a rough
    approximation.
    """
    grouped = df.groupby(["device", "channel"])["ah_cons"]
    ah_max = grouped.transform("max").replace(0, 1.0)
    return 1.0 - (df["ah_cons"] / ah_max)


def prepare_data(
    path: str,
    soh_mode: str = "voltage",
    test_device: str | None = None,
    val_ratio: float = 0.1,
) -> tuple:
    """Load parquet, build features & target, split by device.

    Returns (X_train, y_train, X_val, y_val, X_test, y_test, feature_means, feature_stds)
    all as numpy arrays (float32).
    """
    log.info("Loading %s", path)
    df = pd.read_parquet(path)
    log.info("Raw shape: %s", df.shape)

    # Build SOH proxy before dropping NaNs to maximise coverage
    if soh_mode == "capacity":
        df["soh_proxy"] = build_soh_proxy_capacity(df)
    else:
        df["soh_proxy"] = build_soh_proxy_voltage(df)

    # Drop rows where any feature or target is NaN
    required = FEATURE_COLS + ["soh_proxy", "device", "channel"]
    df = df.dropna(subset=required)
    log.info("After dropna: %s", df.shape)

    if df.empty:
        log.error("No rows left after dropping NaN — check your data.")
        sys.exit(1)

    # --- Split by device (avoids data leakage) ---
    devices = sorted(df["device"].unique().tolist())
    log.info("Devices found: %s", devices)

    if len(devices) >= 3:
        # Device-aware split with broader training coverage:
        # - hold out one device for test (explicit or smallest dataset)
        # - train on remaining devices
        # - carve validation from train rows with deterministic shuffle
        dev_sizes = df.groupby("device").size().sort_values(ascending=False)
        if test_device and test_device in devices:
            test_devs = [test_device]
        else:
            test_devs = [dev_sizes.index[-1]]
        train_devs = [d for d in devices if d not in test_devs]
        val_devs = None
    elif len(devices) == 2:
        # Keep one device for test, train/val split on the other.
        train_devs = [devices[0]]
        test_devs = [devices[1]]
        val_devs = None
    else:
        # Single device — fall back to random 80/10/10
        log.warning("Only 1 device — falling back to random split (leakage possible).")
        rng = np.random.RandomState(42)
        idx = rng.permutation(len(df))
        n_train = int(0.8 * len(df))
        n_val = int(0.1 * len(df))
        train_devs = val_devs = test_devs = None  # sentinel
        train_idx, val_idx, test_idx = idx[:n_train], idx[n_train:n_train + n_val], idx[n_train + n_val:]

    if train_devs is not None:
        log.info("Split — train devices: %s  test devices: %s", train_devs, test_devs)
        train_pool = df[df["device"].isin(train_devs)].copy()
        df_test = df[df["device"].isin(test_devs)].copy()

        rng = np.random.RandomState(42)
        idx = rng.permutation(len(train_pool))
        n_val = max(1, int(val_ratio * len(train_pool)))
        val_idx = idx[:n_val]
        train_idx = idx[n_val:]
        df_val = train_pool.iloc[val_idx]
        df_train = train_pool.iloc[train_idx]
    else:
        df_np = df.to_numpy()  # not used, just for indexing
        df_train = df.iloc[train_idx]
        df_val = df.iloc[val_idx]
        df_test = df.iloc[test_idx]

    log.info("Train: %d  Val: %d  Test: %d", len(df_train), len(df_val), len(df_test))

    # Extract numpy arrays
    X_train = df_train[FEATURE_COLS].values.astype(np.float32)
    y_train = df_train["soh_proxy"].values.astype(np.float32)
    X_val = df_val[FEATURE_COLS].values.astype(np.float32)
    y_val = df_val["soh_proxy"].values.astype(np.float32)
    X_test = df_test[FEATURE_COLS].values.astype(np.float32)
    y_test = df_test["soh_proxy"].values.astype(np.float32)

    # Normalise features (z-score on train set only)
    means = X_train.mean(axis=0)
    stds = X_train.std(axis=0)
    stds[stds == 0] = 1.0  # avoid div-by-zero for constant columns

    X_train = (X_train - means) / stds
    X_val = (X_val - means) / stds
    X_test = (X_test - means) / stds

    return X_train, y_train, X_val, y_val, X_test, y_test, means, stds


# ---------------------------------------------------------------------------
# Metrics
# ---------------------------------------------------------------------------

def compute_metrics(y_true: np.ndarray, y_pred: np.ndarray) -> dict:
    """MAPE (%), RMSE, R-squared.

    MAPE is computed only on samples where y_true > 0.05 to avoid
    division-by-near-zero inflation (common with normalised targets).
    """
    rmse = float(np.sqrt(np.mean((y_true - y_pred) ** 2)))
    ss_res = np.sum((y_true - y_pred) ** 2)
    ss_tot = np.sum((y_true - y_true.mean()) ** 2)
    r2 = float(1.0 - ss_res / (ss_tot + 1e-8))
    # MAPE — filter out near-zero targets
    mask = y_true > 0.05
    if mask.sum() > 0:
        mape = float(np.mean(np.abs((y_true[mask] - y_pred[mask]) / y_true[mask])) * 100.0)
    else:
        mape = float("nan")
    return {"MAPE": mape, "RMSE": rmse, "R2": r2}


# ---------------------------------------------------------------------------
# Training loop
# ---------------------------------------------------------------------------

def train(args: argparse.Namespace) -> None:
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    log.info("Torch device: %s", device)

    # Data
    X_tr, y_tr, X_va, y_va, X_te, y_te, f_means, f_stds = prepare_data(
        args.input,
        soh_mode=args.soh_mode,
        test_device=args.test_device,
        val_ratio=args.val_ratio,
    )
    n_features = X_tr.shape[1]
    log.info("Input features: %d", n_features)

    train_ds = TensorDataset(torch.from_numpy(X_tr), torch.from_numpy(y_tr))
    val_ds = TensorDataset(torch.from_numpy(X_va), torch.from_numpy(y_va))
    train_loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True, drop_last=False)
    val_loader = DataLoader(val_ds, batch_size=args.batch_size * 2, shuffle=False)

    # Model
    model = FPNN(n_features, hidden=args.hidden, degree=args.degree, dropout=args.dropout).to(device)
    n_params = sum(p.numel() for p in model.parameters())
    log.info("FPNN — degree=%d  hidden=%d  params=%d  poly_features=%d",
             args.degree, args.hidden, n_params, model.poly.out_features)

    criterion = nn.MSELoss()
    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)

    # Early stopping state
    best_val_loss = float("inf")
    best_epoch = -1
    patience_counter = 0
    best_state = None

    t0 = time.time()

    for epoch in range(1, args.epochs + 1):
        # --- Train ---
        model.train()
        train_loss_sum = 0.0
        train_n = 0
        for xb, yb in train_loader:
            xb, yb = xb.to(device), yb.to(device)
            pred = model(xb)
            loss = criterion(pred, yb)
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            train_loss_sum += loss.item() * len(xb)
            train_n += len(xb)
        train_loss = train_loss_sum / train_n

        # --- Validate ---
        model.eval()
        val_loss_sum = 0.0
        val_n = 0
        with torch.no_grad():
            for xb, yb in val_loader:
                xb, yb = xb.to(device), yb.to(device)
                pred = model(xb)
                loss = criterion(pred, yb)
                val_loss_sum += loss.item() * len(xb)
                val_n += len(xb)
        val_loss = val_loss_sum / val_n

        # Logging
        if epoch == 1 or epoch % 10 == 0 or epoch == args.epochs:
            elapsed = time.time() - t0
            log.info("Epoch %3d/%d  train_loss=%.6f  val_loss=%.6f  (%.1fs)",
                     epoch, args.epochs, train_loss, val_loss, elapsed)

        # Early stopping
        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_epoch = epoch
            patience_counter = 0
            best_state = {k: v.cpu().clone() for k, v in model.state_dict().items()}
        else:
            patience_counter += 1
            if patience_counter >= args.patience:
                log.info("Early stopping at epoch %d (best epoch %d, val_loss=%.6f)",
                         epoch, best_epoch, best_val_loss)
                break

    # Restore best weights
    if best_state is not None:
        model.load_state_dict(best_state)
        model.to(device)
    log.info("Best epoch: %d  best val_loss: %.6f", best_epoch, best_val_loss)

    # --- Test evaluation ---
    model.eval()
    with torch.no_grad():
        X_te_t = torch.from_numpy(X_te).to(device)
        y_pred = model(X_te_t).cpu().numpy()
    metrics = compute_metrics(y_te, y_pred)
    log.info("Test metrics — MAPE: %.2f%%  RMSE: %.6f  R2: %.4f",
             metrics["MAPE"], metrics["RMSE"], metrics["R2"])

    # --- Save ---
    out_dir = Path(args.output_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    # PyTorch checkpoint (includes normalisation stats for inference)
    pt_path = out_dir / "fpnn_soh.pt"
    torch.save({
        "model_state_dict": model.state_dict(),
        "n_features": n_features,
        "hidden": args.hidden,
        "degree": args.degree,
        "dropout": args.dropout,
        "feature_cols": FEATURE_COLS,
        "feature_means": f_means.tolist(),
        "feature_stds": f_stds.tolist(),
        "soh_mode": args.soh_mode,
        "best_epoch": best_epoch,
        "best_val_loss": best_val_loss,
        "test_metrics": metrics,
    }, pt_path)
    pt_kb = pt_path.stat().st_size / 1024
    log.info("Saved PyTorch model: %s (%.1f KB)", pt_path, pt_kb)

    # ONNX export
    onnx_path = out_dir / "fpnn_soh.onnx"
    model.to("cpu")
    dummy = torch.randn(1, n_features)
    try:
        # Try the modern dynamo-based exporter first, fall back to legacy
        try:
            torch.onnx.export(
                model,
                dummy,
                str(onnx_path),
                input_names=["features"],
                output_names=["soh"],
                dynamic_axes={"features": {0: "batch"}, "soh": {0: "batch"}},
                opset_version=13,
                dynamo=False,  # force legacy TorchScript exporter (no onnxscript dep)
            )
        except TypeError:
            # Older PyTorch without dynamo kwarg
            torch.onnx.export(
                model,
                dummy,
                str(onnx_path),
                input_names=["features"],
                output_names=["soh"],
                dynamic_axes={"features": {0: "batch"}, "soh": {0: "batch"}},
                opset_version=13,
            )
        onnx_kb = onnx_path.stat().st_size / 1024
        log.info("Saved ONNX model: %s (%.1f KB)", onnx_path, onnx_kb)
    except Exception as exc:
        log.warning("ONNX export failed: %s — install 'onnx' package to enable", exc)

    # Summary
    log.info("=" * 60)
    log.info("FPNN Training Summary")
    log.info("  Features:     %d  (degree-%d expansion -> %d)", n_features, args.degree, model.poly.out_features)
    log.info("  Parameters:   %d", n_params)
    log.info("  Best epoch:   %d / %d", best_epoch, args.epochs)
    log.info("  Val MSE:      %.6f", best_val_loss)
    log.info("  Test MAPE:    %.2f%%", metrics["MAPE"])
    log.info("  Test RMSE:    %.6f", metrics["RMSE"])
    log.info("  Test R2:      %.4f", metrics["R2"])
    log.info("  Model size:   %.1f KB (pt)  %.1f KB (onnx)",
             pt_kb, onnx_path.stat().st_size / 1024 if onnx_path.exists() else 0)
    log.info("=" * 60)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Train FPNN model for battery SOH prediction",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument("--input", required=True, help="Path to features.parquet")
    p.add_argument("--output-dir", default="models", help="Directory to save models")
    p.add_argument("--epochs", type=int, default=100)
    p.add_argument("--lr", type=float, default=1e-3)
    p.add_argument("--weight-decay", type=float, default=1e-4)
    p.add_argument("--hidden", type=int, default=32)
    p.add_argument("--degree", type=int, default=3, choices=[2, 3])
    p.add_argument("--dropout", type=float, default=0.1)
    p.add_argument("--batch-size", type=int, default=1024)
    p.add_argument("--patience", type=int, default=10, help="Early stopping patience")
    p.add_argument("--test-device", default=None, help="Optional device to hold out for test split")
    p.add_argument("--val-ratio", type=float, default=0.1, help="Validation ratio carved from train pool")
    p.add_argument(
        "--soh-mode",
        default="capacity",
        choices=["voltage", "capacity"],
        help="SOH proxy method: 'voltage' = normalised rest_voltage, "
             "'capacity' = capacity fade from ah_cons (Phase 2.2 FIX: device-independent)",
    )
    return p.parse_args()


if __name__ == "__main__":
    args = parse_args()
    train(args)
