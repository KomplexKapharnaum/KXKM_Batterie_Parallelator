#!/usr/bin/env python3
"""Finalize Phase 2 metrics into a machine-readable JSON artifact.

This script aggregates:
- FPNN float metrics (computed from checkpoint + features)
- FPNN quantized metrics (if ONNX quantized model exists)
- Model sizes and threshold gates
- Optional training-log values (if phase2_fpnn_train.log is present)
- SambaMixer artifact metadata
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

import torch


def _kb(path: Path) -> float:
    return round(path.stat().st_size / 1024.0, 2)


def _parse_train_log(log_path: Path) -> dict:
    if not log_path.exists():
        return {}
    text = log_path.read_text(encoding="utf-8", errors="ignore")
    out: dict[str, float | int] = {}

    m = re.search(r"Test metrics\s*-\s*MAPE:\s*([0-9.]+)%\s*RMSE:\s*([0-9.]+)\s*R2:\s*([\-0-9.]+)", text)
    if m:
        out["test_mape_from_log"] = float(m.group(1))
        out["test_rmse_from_log"] = float(m.group(2))
        out["test_r2_from_log"] = float(m.group(3))

    m = re.search(r"Best epoch:\s*([0-9]+)\s*best val_loss:\s*([0-9.]+)", text)
    if m:
        out["best_epoch"] = int(m.group(1))
        out["best_val_loss"] = float(m.group(2))

    return out


def main() -> None:
    parser = argparse.ArgumentParser(description="Aggregate final Phase 2 ML metrics")
    parser.add_argument("--model", default="models/fpnn_soh.pt", help="Path to FPNN checkpoint")
    parser.add_argument("--features", default="models/features_adapted.parquet", help="Path to features parquet")
    parser.add_argument("--quantized", default="models/fpnn_soh_v2_quantized.onnx", help="Path to quantized ONNX model")
    parser.add_argument("--rul-model", default="models/rul_sambamixer.pt", help="Path to SambaMixer checkpoint")
    parser.add_argument("--train-log", default="phase2_fpnn_train.log", help="Optional training log path")
    parser.add_argument("--output", default="models/phase2_metrics.json", help="Output JSON path")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    sys.path.insert(0, str(repo_root / "scripts" / "ml"))

    import quantize_tflite as q  # local script import

    model_path = (repo_root / args.model).resolve() if not Path(args.model).is_absolute() else Path(args.model)
    features_path = (repo_root / args.features).resolve() if not Path(args.features).is_absolute() else Path(args.features)
    quantized_path = (repo_root / args.quantized).resolve() if not Path(args.quantized).is_absolute() else Path(args.quantized)
    rul_model_path = (repo_root / args.rul_model).resolve() if not Path(args.rul_model).is_absolute() else Path(args.rul_model)
    train_log_path = (repo_root / args.train_log).resolve() if not Path(args.train_log).is_absolute() else Path(args.train_log)
    output_path = (repo_root / args.output).resolve() if not Path(args.output).is_absolute() else Path(args.output)

    if not model_path.exists():
        raise FileNotFoundError(f"FPNN checkpoint not found: {model_path}")
    if not features_path.exists():
        raise FileNotFoundError(f"Features parquet not found: {features_path}")

    checkpoint = torch.load(model_path, map_location="cpu", weights_only=False)
    n_features = checkpoint["n_features"]
    hidden = checkpoint["hidden"]
    degree = checkpoint["degree"]
    dropout = checkpoint.get("dropout", 0.1)

    model = q.FPNN(n_features, hidden=hidden, degree=degree, dropout=dropout)
    model.load_state_dict(checkpoint["model_state_dict"])
    model.eval()

    X_all, y_all = q.load_data(str(features_path), checkpoint)
    n_test = max(1, int(0.2 * len(X_all)))
    X_test = X_all[-n_test:]
    y_test = y_all[-n_test:]

    pt_metrics = q.validate_pytorch(model, X_test, y_test)

    quant_metrics = None
    if quantized_path.exists() and quantized_path.suffix == ".onnx":
        quant_metrics = q.validate_onnxrt(quantized_path, X_test, y_test)

    log_metrics = _parse_train_log(train_log_path)

    pt_size_kb = _kb(model_path)
    quant_size_kb = _kb(quantized_path) if quantized_path.exists() else None
    rul_size_kb = _kb(rul_model_path) if rul_model_path.exists() else None

    mape_degradation_pp = None
    if quant_metrics is not None:
        mape_degradation_pp = round(quant_metrics["MAPE"] - pt_metrics["MAPE"], 4)

    gates = {
        "fpnn_mape_le_15": bool(pt_metrics["MAPE"] <= 15.0),
        "quantized_size_lt_50kb": bool(quant_size_kb is not None and quant_size_kb < 50.0),
        "quantized_mape_degradation_le_5pp": bool(
            mape_degradation_pp is not None and abs(mape_degradation_pp) <= 5.0
        ),
    }

    payload = {
        "phase": "3A Phase 2",
        "status": "in-progress",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "inputs": {
            "model": str(model_path),
            "features": str(features_path),
            "quantized": str(quantized_path),
            "rul_model": str(rul_model_path),
            "train_log": str(train_log_path),
        },
        "fpnn": {
            "soh_mode": checkpoint.get("soh_mode", "unknown"),
            "degree": degree,
            "hidden": hidden,
            "n_features": n_features,
            "params": int(sum(p.numel() for p in model.parameters())),
            "size_kb": pt_size_kb,
            "metrics_test_split_20pct": {
                "mape": round(float(pt_metrics["MAPE"]), 4),
                "rmse": round(float(pt_metrics["RMSE"]), 6),
                "r2": round(float(pt_metrics["R2"]), 6),
                "n_samples": int(len(X_test)),
            },
        },
        "fpnn_quantized": {
            "exists": bool(quantized_path.exists()),
            "path": str(quantized_path),
            "size_kb": quant_size_kb,
            "metrics_test_split_20pct": {
                "mape": round(float(quant_metrics["MAPE"]), 4) if quant_metrics else None,
                "rmse": round(float(quant_metrics["RMSE"]), 6) if quant_metrics else None,
                "r2": round(float(quant_metrics["R2"]), 6) if quant_metrics else None,
            },
            "mape_degradation_pp": mape_degradation_pp,
        },
        "sambamixer": {
            "exists": bool(rul_model_path.exists()),
            "path": str(rul_model_path),
            "size_kb": rul_size_kb,
        },
        "log_extract": log_metrics,
        "gates": gates,
        "overall_gate_pass": bool(all(gates.values())),
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
    print(f"Wrote metrics: {output_path}")
    print(f"Overall gate pass: {payload['overall_gate_pass']}")


if __name__ == "__main__":
    main()
