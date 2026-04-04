"""Inference runner for SOH scoring pipeline.

Loads ONNX models (TSMixer) and PyTorch (GNN) for battery scoring.
Designed to run on a 30-min cron cycle on kxkm-ai.
"""

from __future__ import annotations

import logging
from pathlib import Path

import numpy as np
import onnxruntime as ort
import torch

from soh.config import settings
from soh.gnn import FleetGNN, build_fleet_graph

logger = logging.getLogger(__name__)


class InferenceRunner:
    """Runs TSMixer + GNN inference on battery feature data."""

    def __init__(
        self,
        tsmixer_path: str | None = None,
        gnn_path: str | None = None,
        rul_max: float = 2000.0,
    ):
        self.rul_max = rul_max

        # Load TSMixer ONNX session
        tsmixer_path = tsmixer_path or settings.tsmixer_onnx_path
        if Path(tsmixer_path).exists():
            self.tsmixer_session = ort.InferenceSession(
                tsmixer_path,
                providers=["CUDAExecutionProvider", "CPUExecutionProvider"],
            )
            logger.info("Loaded TSMixer ONNX: %s", tsmixer_path)
        else:
            self.tsmixer_session = None
            logger.warning("TSMixer ONNX not found: %s", tsmixer_path)

        # Load GNN (PyTorch, since PyG is not trivially ONNX-exportable)
        gnn_path = gnn_path or settings.gnn_onnx_path
        if gnn_path and Path(gnn_path).exists():
            ckpt = torch.load(gnn_path, map_location="cpu", weights_only=False)
            self.gnn_model = FleetGNN(
                node_features=ckpt.get("node_features", settings.gnn_node_features),
                hidden=ckpt.get("hidden", settings.gnn_hidden),
                n_layers=ckpt.get("n_layers", settings.gnn_n_layers),
                heads=ckpt.get("heads", settings.gnn_heads),
            )
            self.gnn_model.load_state_dict(ckpt["model_state_dict"])
            self.gnn_model.eval()
            logger.info("Loaded FleetGNN: %s", gnn_path)
        else:
            self.gnn_model = None
            logger.warning("FleetGNN not found: %s", gnn_path)

    def score_batteries(
        self,
        features: dict[int, np.ndarray],
    ) -> dict[int, dict]:
        """Score all batteries using TSMixer.

        Parameters
        ----------
        features : dict mapping battery_id -> (seq_len, 17) feature matrix

        Returns
        -------
        dict mapping battery_id -> {soh_score, rul_days, anomaly_score}
        """
        if not features or self.tsmixer_session is None:
            return {}

        results = {}

        # Batch inference
        bat_ids = sorted(features.keys())
        batch = np.stack([features[bid] for bid in bat_ids])  # (N, seq_len, 17)

        outputs = self.tsmixer_session.run(
            None,
            {"features": batch.astype(np.float32)},
        )

        soh_scores = outputs[0]    # (N,)
        rul_scores = outputs[1]    # (N,)
        anom_scores = outputs[2]   # (N,)

        for i, bat_id in enumerate(bat_ids):
            results[bat_id] = {
                "soh_score": float(np.clip(soh_scores[i], 0.0, 1.0)),
                "rul_days": float(max(0, rul_scores[i] * self.rul_max)),
                "anomaly_score": float(np.clip(anom_scores[i], 0.0, 1.0)),
            }

        return results

    def score_fleet(
        self,
        battery_scores: dict[int, dict],
        features: dict[int, np.ndarray],
    ) -> dict | None:
        """Score fleet-level health using GNN.

        Parameters
        ----------
        battery_scores : output from score_batteries
        features : dict mapping battery_id -> (seq_len, 17) feature matrix

        Returns
        -------
        dict with fleet_health, outlier_idx, outlier_score, imbalance_severity
        """
        if self.gnn_model is None or len(battery_scores) < 2:
            return None

        bat_ids = sorted(battery_scores.keys())
        n = len(bat_ids)

        # Build node features (use mean of feature matrix as embedding, padded to 64)
        node_feats = np.zeros((n, settings.gnn_node_features), dtype=np.float32)
        voltages = np.zeros(n, dtype=np.float32)
        currents = np.zeros(n, dtype=np.float32)

        for i, bid in enumerate(bat_ids):
            feat = features[bid]
            mean_feat = feat.mean(axis=0)  # (17,)
            node_feats[i, :len(mean_feat)] = mean_feat
            voltages[i] = feat[:, 5].mean()   # v_mean
            currents[i] = feat[:, 10].mean()  # i_mean

        node_feats_t = torch.from_numpy(node_feats)
        voltages_t = torch.from_numpy(voltages)
        currents_t = torch.from_numpy(currents)

        edge_index, edge_attr = build_fleet_graph(node_feats_t, voltages_t, currents_t)

        with torch.no_grad():
            fleet_health, outlier_local_idx, outlier_score, imbalance = self.gnn_model(
                node_feats_t, edge_index, edge_attr
            )

        return {
            "fleet_health": float(fleet_health.item()),
            "outlier_idx": bat_ids[outlier_local_idx],
            "outlier_score": float(outlier_score.item()),
            "imbalance_severity": float(imbalance.item()),
        }
