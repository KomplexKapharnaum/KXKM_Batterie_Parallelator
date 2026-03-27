#!/usr/bin/env python3
"""
quantize_tflite.py — Quantize the FPNN battery SOH model for edge deployment.

Pipeline:
    1. Load PyTorch .pt checkpoint
    2. Export to ONNX (if not already present alongside the .pt)
    3. Convert ONNX -> TFLite (via onnx2tf or tf.lite.TFLiteConverter)
       OR fallback: ONNX Runtime INT8 quantization (onnxruntime.quantization)
    4. Apply INT8 post-training quantization using representative data
    5. Save quantized model
    6. Print size comparison (float32 vs INT8)
    7. Validate: compare MAPE between original and quantized on test set

Install requirements (pick ONE path):

    Path A — TFLite (preferred for microcontroller deployment):
        pip install torch onnx onnx2tf tensorflow numpy pandas pyarrow

    Path B — ONNX Runtime quantization (fallback, no TF dependency):
        pip install torch onnx onnxruntime numpy pandas pyarrow

Usage:
    python scripts/ml/quantize_tflite.py \\
        --model models/fpnn_soh.pt \\
        --features data/features.parquet \\
        --output models/fpnn_soh_int8.tflite

    # Force ONNX Runtime fallback even if TFLite is available:
    python scripts/ml/quantize_tflite.py \\
        --model models/fpnn_soh.pt \\
        --features data/features.parquet \\
        --output models/fpnn_soh_int8.onnx \\
        --backend onnxrt
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

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("quantize")

# ---------------------------------------------------------------------------
# Feature columns — must match train_fpnn.py exactly
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
# FPNN model definition — duplicated from train_fpnn.py to keep script
# self-contained (no relative imports needed).
# ---------------------------------------------------------------------------
class PolynomialExpansion(nn.Module):
    """Expand input features to include all monomials up to *degree*."""

    def __init__(self, n_features: int, degree: int = 3):
        super().__init__()
        self.n_features = n_features
        self.degree = degree
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
        a = x[:, self.pair_idx[:, 0]]
        b = x[:, self.pair_idx[:, 1]]
        parts.append(a * b)
        if self.degree >= 3 and self.triple_idx.shape[0] > 0:
            a3 = x[:, self.triple_idx[:, 0]]
            b3 = x[:, self.triple_idx[:, 1]]
            c3 = x[:, self.triple_idx[:, 2]]
            parts.append(a3 * b3 * c3)
        return torch.cat(parts, dim=1)


class FPNN(nn.Module):
    """Feature-based Polynomial Neural Network."""

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
# Data loading (reuses train_fpnn.py logic for SOH proxy + normalisation)
# ---------------------------------------------------------------------------

def build_soh_proxy_voltage(df: pd.DataFrame) -> pd.Series:
    grouped = df.groupby(["device", "channel"])["rest_voltage"]
    vmin = grouped.transform("min")
    vmax = grouped.transform("max")
    span = (vmax - vmin).replace(0, 1.0)
    return (df["rest_voltage"] - vmin) / span


def build_soh_proxy_capacity(df: pd.DataFrame) -> pd.Series:
    grouped = df.groupby(["device", "channel"])["ah_cons"]
    ah_max = grouped.transform("max").replace(0, 1.0)
    return 1.0 - (df["ah_cons"] / ah_max)


def load_data(features_path: str, checkpoint: dict) -> tuple:
    """Load features.parquet and prepare normalised arrays using checkpoint stats.

    Returns (X_all, y_all) as float32 numpy arrays, normalised with the
    same means/stds that were used during training.
    """
    log.info("Loading features from %s", features_path)
    df = pd.read_parquet(features_path)

    soh_mode = checkpoint.get("soh_mode", "voltage")
    if soh_mode == "capacity":
        df["soh_proxy"] = build_soh_proxy_capacity(df)
    else:
        df["soh_proxy"] = build_soh_proxy_voltage(df)

    required = FEATURE_COLS + ["soh_proxy"]
    df = df.dropna(subset=required)
    log.info("Usable rows after dropna: %d", len(df))

    if df.empty:
        log.error("No valid rows in features file.")
        sys.exit(1)

    X = df[FEATURE_COLS].values.astype(np.float32)
    y = df["soh_proxy"].values.astype(np.float32)

    # Normalise using training-time statistics from checkpoint
    means = np.array(checkpoint["feature_means"], dtype=np.float32)
    stds = np.array(checkpoint["feature_stds"], dtype=np.float32)
    stds[stds == 0] = 1.0
    X = (X - means) / stds

    return X, y


# ---------------------------------------------------------------------------
# ONNX export
# ---------------------------------------------------------------------------

def export_onnx(model: FPNN, n_features: int, onnx_path: Path) -> Path:
    """Export PyTorch model to ONNX if the file does not already exist."""
    if onnx_path.exists():
        log.info("ONNX file already exists: %s", onnx_path)
        return onnx_path

    log.info("Exporting to ONNX: %s", onnx_path)
    model.eval()
    dummy = torch.randn(1, n_features)
    try:
        torch.onnx.export(
            model,
            dummy,
            str(onnx_path),
            input_names=["features"],
            output_names=["soh"],
            dynamic_axes={"features": {0: "batch"}, "soh": {0: "batch"}},
            opset_version=13,
            dynamo=False,
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
    log.info("ONNX export done (%.1f KB)", onnx_path.stat().st_size / 1024)
    return onnx_path


# ---------------------------------------------------------------------------
# Path A: ONNX -> TFLite via onnx2tf + TFLite converter
# ---------------------------------------------------------------------------

def quantize_tflite(onnx_path: Path, output_path: Path, X_repr: np.ndarray) -> Path:
    """Convert ONNX to TFLite with INT8 post-training quantization.

    Uses onnx2tf to convert ONNX -> SavedModel, then tf.lite.TFLiteConverter
    with a representative dataset for full INT8 quantization.
    """
    import onnx2tf  # noqa: F811
    import tensorflow as tf

    # Step 1: ONNX -> TF SavedModel
    saved_model_dir = output_path.parent / "_tflite_savedmodel"
    log.info("Converting ONNX -> SavedModel via onnx2tf ...")
    onnx2tf.convert(
        input_onnx_file_path=str(onnx_path),
        output_folder_path=str(saved_model_dir),
        non_verbose=True,
    )
    log.info("SavedModel written to %s", saved_model_dir)

    # Step 2: SavedModel -> TFLite (float32, for size comparison)
    converter_f32 = tf.lite.TFLiteConverter.from_saved_model(str(saved_model_dir))
    tflite_f32 = converter_f32.convert()
    f32_path = output_path.with_suffix(".f32.tflite")
    f32_path.write_bytes(tflite_f32)
    log.info("Float32 TFLite: %s (%.1f KB)", f32_path, len(tflite_f32) / 1024)

    # Step 3: SavedModel -> TFLite INT8 with representative dataset
    def representative_dataset():
        # Yield 200 samples (or all if fewer) for calibration
        n_samples = min(200, len(X_repr))
        indices = np.random.default_rng(42).choice(len(X_repr), n_samples, replace=False)
        for i in indices:
            yield [X_repr[i:i+1].astype(np.float32)]

    converter_int8 = tf.lite.TFLiteConverter.from_saved_model(str(saved_model_dir))
    converter_int8.optimizations = [tf.lite.Optimize.DEFAULT]
    converter_int8.representative_dataset = representative_dataset
    # Full integer quantization (input/output remain float for compatibility)
    converter_int8.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter_int8.inference_input_type = tf.int8
    converter_int8.inference_output_type = tf.float32

    try:
        tflite_int8 = converter_int8.convert()
    except Exception as exc:
        log.warning("Full INT8 failed (%s), falling back to dynamic range quantization", exc)
        converter_int8 = tf.lite.TFLiteConverter.from_saved_model(str(saved_model_dir))
        converter_int8.optimizations = [tf.lite.Optimize.DEFAULT]
        converter_int8.representative_dataset = representative_dataset
        tflite_int8 = converter_int8.convert()

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(tflite_int8)
    log.info("INT8 TFLite: %s (%.1f KB)", output_path, len(tflite_int8) / 1024)

    # Cleanup SavedModel temp dir
    import shutil
    shutil.rmtree(saved_model_dir, ignore_errors=True)

    return output_path


def validate_tflite(tflite_path: Path, X_test: np.ndarray, y_test: np.ndarray) -> dict:
    """Run TFLite inference and compute metrics."""
    import tensorflow as tf

    interpreter = tf.lite.Interpreter(model_path=str(tflite_path))
    interpreter.allocate_tensors()

    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()

    input_dtype = input_details[0]["dtype"]
    input_scale = input_details[0].get("quantization_parameters", {}).get("scales", [])
    input_zp = input_details[0].get("quantization_parameters", {}).get("zero_points", [])

    predictions = []
    for i in range(len(X_test)):
        sample = X_test[i:i+1].astype(np.float32)
        # Quantize input if the model expects int8
        if input_dtype == np.int8 and len(input_scale) > 0 and input_scale[0] != 0:
            sample = (sample / input_scale[0] + input_zp[0]).astype(np.int8)
        interpreter.set_tensor(input_details[0]["index"], sample)
        interpreter.invoke()
        out = interpreter.get_tensor(output_details[0]["index"])
        predictions.append(float(out.flatten()[0]))

    y_pred = np.array(predictions, dtype=np.float32)
    return _compute_metrics(y_test, y_pred)


# ---------------------------------------------------------------------------
# Path B: ONNX Runtime quantization (fallback)
# ---------------------------------------------------------------------------

def quantize_onnxrt(onnx_path: Path, output_path: Path, X_repr: np.ndarray) -> Path:
    """Quantize ONNX model to INT8 using onnxruntime.quantization.

    This is the fallback when TensorFlow/onnx2tf are not available.
    Output is an INT8-quantized .onnx file.
    """
    from onnxruntime.quantization import (
        CalibrationDataReader,
        QuantType,
        quantize_static,
    )

    class BatteryCalibrationReader(CalibrationDataReader):
        """Feeds representative battery data for INT8 calibration."""

        def __init__(self, X: np.ndarray, n_samples: int = 200):
            rng = np.random.default_rng(42)
            n = min(n_samples, len(X))
            self.indices = rng.choice(len(X), n, replace=False)
            self.X = X
            self.pos = 0

        def get_next(self):
            if self.pos >= len(self.indices):
                return None
            idx = self.indices[self.pos]
            self.pos += 1
            return {"features": self.X[idx:idx+1].astype(np.float32)}

    # Ensure output has .onnx extension for this path
    if output_path.suffix == ".tflite":
        output_path = output_path.with_suffix(".int8.onnx")

    log.info("Quantizing ONNX model with onnxruntime (static INT8) ...")
    calibration_reader = BatteryCalibrationReader(X_repr)

    quantize_static(
        model_input=str(onnx_path),
        model_output=str(output_path),
        calibration_data_reader=calibration_reader,
        quant_format=None,  # default QDQ format
        per_channel=True,
        weight_type=QuantType.QInt8,
    )

    log.info("INT8 ONNX: %s (%.1f KB)", output_path, output_path.stat().st_size / 1024)
    return output_path


def validate_onnxrt(model_path: Path, X_test: np.ndarray, y_test: np.ndarray) -> dict:
    """Run ONNX Runtime inference and compute metrics."""
    import onnxruntime as ort

    sess = ort.InferenceSession(str(model_path))
    input_name = sess.get_inputs()[0].name
    y_pred = sess.run(None, {input_name: X_test.astype(np.float32)})[0].flatten()
    return _compute_metrics(y_test, y_pred)


# ---------------------------------------------------------------------------
# Metrics (mirrors train_fpnn.py)
# ---------------------------------------------------------------------------

def _compute_metrics(y_true: np.ndarray, y_pred: np.ndarray) -> dict:
    rmse = float(np.sqrt(np.mean((y_true - y_pred) ** 2)))
    ss_res = np.sum((y_true - y_pred) ** 2)
    ss_tot = np.sum((y_true - y_true.mean()) ** 2)
    r2 = float(1.0 - ss_res / (ss_tot + 1e-8))
    mask = y_true > 0.05
    if mask.sum() > 0:
        mape = float(np.mean(np.abs((y_true[mask] - y_pred[mask]) / y_true[mask])) * 100.0)
    else:
        mape = float("nan")
    return {"MAPE": mape, "RMSE": rmse, "R2": r2}


# ---------------------------------------------------------------------------
# PyTorch reference inference (for validation baseline)
# ---------------------------------------------------------------------------

def validate_pytorch(model: FPNN, X_test: np.ndarray, y_test: np.ndarray) -> dict:
    """Run PyTorch inference and compute metrics (baseline)."""
    model.eval()
    with torch.no_grad():
        y_pred = model(torch.from_numpy(X_test)).numpy()
    return _compute_metrics(y_test, y_pred)


# ---------------------------------------------------------------------------
# Backend detection
# ---------------------------------------------------------------------------

def detect_backend() -> str:
    """Detect available conversion backend: 'tflite' or 'onnxrt'."""
    try:
        import onnx2tf  # noqa: F401
        import tensorflow  # noqa: F401
        return "tflite"
    except ImportError:
        pass
    try:
        import onnxruntime  # noqa: F401
        from onnxruntime.quantization import quantize_static  # noqa: F401
        return "onnxrt"
    except ImportError:
        pass
    return "none"


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Quantize FPNN battery SOH model to INT8 (TFLite or ONNX Runtime)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--model", default="models/fpnn_soh.pt",
        help="Path to PyTorch checkpoint (.pt)",
    )
    parser.add_argument(
        "--features", default="data/features.parquet",
        help="Path to features.parquet for calibration + validation",
    )
    parser.add_argument(
        "--output", default="models/fpnn_soh_int8.tflite",
        help="Output path for quantized model",
    )
    parser.add_argument(
        "--backend", choices=["tflite", "onnxrt", "auto"], default="auto",
        help="Quantization backend: 'tflite' (ONNX->TFLite), 'onnxrt' (ONNX Runtime INT8), 'auto'",
    )
    args = parser.parse_args()

    t0 = time.time()

    # --- 1. Load checkpoint ---
    model_path = Path(args.model)
    if not model_path.exists():
        log.error("Model not found: %s", model_path)
        sys.exit(1)

    log.info("Loading checkpoint: %s", model_path)
    checkpoint = torch.load(model_path, map_location="cpu", weights_only=False)

    n_features = checkpoint["n_features"]
    hidden = checkpoint["hidden"]
    degree = checkpoint["degree"]
    dropout = checkpoint.get("dropout", 0.1)

    model = FPNN(n_features, hidden=hidden, degree=degree, dropout=dropout)
    model.load_state_dict(checkpoint["model_state_dict"])
    model.eval()

    n_params = sum(p.numel() for p in model.parameters())
    log.info("FPNN loaded — degree=%d, hidden=%d, params=%d", degree, hidden, n_params)

    # --- 2. Load data ---
    features_path = Path(args.features)
    if not features_path.exists():
        log.error("Features file not found: %s", features_path)
        sys.exit(1)

    X_all, y_all = load_data(str(features_path), checkpoint)

    # Use last 20% as test set, rest as calibration representative data
    n_test = max(1, int(0.2 * len(X_all)))
    X_repr = X_all[:-n_test]
    X_test = X_all[-n_test:]
    y_test = y_all[-n_test:]
    log.info("Calibration samples: %d, Test samples: %d", len(X_repr), len(X_test))

    # --- 3. ONNX export ---
    onnx_path = model_path.with_suffix(".onnx")
    export_onnx(model, n_features, onnx_path)

    # --- 4. Detect backend ---
    backend = args.backend
    if backend == "auto":
        backend = detect_backend()
        if backend == "none":
            log.error(
                "No quantization backend available.\n"
                "Install one of:\n"
                "  Path A (TFLite):   pip install onnx2tf tensorflow\n"
                "  Path B (ONNX RT):  pip install onnxruntime\n"
            )
            sys.exit(1)
    log.info("Using backend: %s", backend)

    # --- 5. Quantize ---
    output_path = Path(args.output)

    if backend == "tflite":
        quant_path = quantize_tflite(onnx_path, output_path, X_repr)
        quant_metrics = validate_tflite(quant_path, X_test, y_test)
    else:
        quant_path = quantize_onnxrt(onnx_path, output_path, X_repr)
        quant_metrics = validate_onnxrt(quant_path, X_test, y_test)

    # --- 6. PyTorch baseline metrics ---
    pt_metrics = validate_pytorch(model, X_test, y_test)

    # --- 7. Size comparison ---
    pt_size = model_path.stat().st_size
    onnx_size = onnx_path.stat().st_size
    quant_size = quant_path.stat().st_size

    elapsed = time.time() - t0

    log.info("=" * 64)
    log.info("FPNN Quantization Summary")
    log.info("=" * 64)
    log.info("  Backend:         %s", backend)
    log.info("")
    log.info("  --- Model sizes ---")
    log.info("  PyTorch (.pt):   %7.1f KB", pt_size / 1024)
    log.info("  ONNX (float32):  %7.1f KB", onnx_size / 1024)
    log.info("  Quantized INT8:  %7.1f KB", quant_size / 1024)
    log.info("  Compression:     %.1fx (vs .pt)  %.1fx (vs ONNX)",
             pt_size / max(quant_size, 1), onnx_size / max(quant_size, 1))
    log.info("")
    log.info("  --- Accuracy (test set, %d samples) ---", len(X_test))
    log.info("  %-18s  MAPE %%    RMSE      R2", "Model")
    log.info("  %-18s  %6.2f    %.6f  %.4f", "PyTorch (float32)",
             pt_metrics["MAPE"], pt_metrics["RMSE"], pt_metrics["R2"])
    log.info("  %-18s  %6.2f    %.6f  %.4f", "Quantized (INT8)",
             quant_metrics["MAPE"], quant_metrics["RMSE"], quant_metrics["R2"])
    mape_delta = quant_metrics["MAPE"] - pt_metrics["MAPE"]
    log.info("")
    log.info("  MAPE degradation: %+.2f pp", mape_delta)
    if abs(mape_delta) < 1.0:
        log.info("  -> Quantization quality: EXCELLENT (< 1pp MAPE loss)")
    elif abs(mape_delta) < 3.0:
        log.info("  -> Quantization quality: GOOD (< 3pp MAPE loss)")
    else:
        log.warning("  -> Quantization quality: DEGRADED (>= 3pp MAPE loss)")
    log.info("")
    log.info("  Output: %s", quant_path)
    log.info("  Time:   %.1f s", elapsed)
    log.info("=" * 64)


if __name__ == "__main__":
    main()
