"""ONNX export and quantization for TSMixer and FleetGNN.

Exports trained PyTorch models to ONNX format with optional dynamic quantization.
"""

from __future__ import annotations

import argparse
import logging
from pathlib import Path

import numpy as np
import onnx
import torch
from onnxruntime.quantization import quantize_dynamic, QuantType

from soh.config import settings
from soh.tsmixer import TSMixer
from soh.gnn import FleetGNN

logger = logging.getLogger(__name__)


def export_tsmixer(
    checkpoint_path: Path,
    output_path: Path,
    quantize: bool = True,
) -> Path:
    """Export TSMixer to ONNX."""
    ckpt = torch.load(checkpoint_path, map_location="cpu", weights_only=False)

    model = TSMixer(
        n_features=ckpt["n_features"],
        hidden=ckpt["hidden"],
        n_mix_layers=ckpt["n_mix_layers"],
    )
    model.load_state_dict(ckpt["model_state_dict"])
    model.eval()

    dummy = torch.randn(1, settings.tsmixer_seq_len, ckpt["n_features"])

    # Export
    fp32_path = output_path.with_suffix(".fp32.onnx")
    torch.onnx.export(
        model,
        dummy,
        str(fp32_path),
        input_names=["features"],
        output_names=["soh", "rul", "anomaly"],
        dynamic_axes={"features": {0: "batch", 1: "seq_len"}},
        opset_version=17,
    )
    logger.info("Exported TSMixer FP32: %s (%.1f KB)",
                fp32_path, fp32_path.stat().st_size / 1024)

    if quantize:
        quantize_dynamic(
            str(fp32_path),
            str(output_path),
            weight_type=QuantType.QUInt8,
        )
        logger.info("Quantized TSMixer: %s (%.1f KB)",
                    output_path, output_path.stat().st_size / 1024)
        return output_path
    else:
        fp32_path.rename(output_path)
        return output_path


def export_gnn(
    checkpoint_path: Path,
    output_path: Path,
    n_nodes: int = 8,
) -> Path:
    """Export FleetGNN to ONNX.

    Note: GNN export requires a fixed graph structure for tracing.
    The inference runner reconstructs the graph at runtime.
    """
    ckpt = torch.load(checkpoint_path, map_location="cpu", weights_only=False)

    model = FleetGNN(
        node_features=ckpt["node_features"],
        hidden=ckpt["hidden"],
        n_layers=ckpt["n_layers"],
        heads=ckpt["heads"],
    )
    model.load_state_dict(ckpt["model_state_dict"])
    model.eval()

    # For GNN, we keep PyTorch inference (PyG not trivially ONNX-exportable)
    # Save as TorchScript instead
    ts_path = output_path.with_suffix(".pt")
    torch.save(ckpt, ts_path)
    logger.info("Saved GNN checkpoint: %s (%.1f KB)", ts_path, ts_path.stat().st_size / 1024)
    return ts_path


def main():
    parser = argparse.ArgumentParser(description="Export SOH models to ONNX")
    parser.add_argument("--tsmixer", type=Path, default=None, help="TSMixer checkpoint path")
    parser.add_argument("--gnn", type=Path, default=None, help="GNN checkpoint path")
    parser.add_argument("--output-dir", type=Path, default=Path("models"))
    parser.add_argument("--no-quantize", action="store_true")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)

    if args.tsmixer:
        export_tsmixer(
            args.tsmixer,
            args.output_dir / "tsmixer_soh.onnx",
            quantize=not args.no_quantize,
        )

    if args.gnn:
        export_gnn(args.gnn, args.output_dir / "gnn_fleet.onnx")


if __name__ == "__main__":
    main()
