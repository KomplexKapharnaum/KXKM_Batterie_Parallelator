"""Tests for ONNX inference pipeline."""

import numpy as np
import pytest
import torch

from soh.tsmixer import TSMixer
from soh.inference import InferenceRunner


class TestInferenceRunner:
    @pytest.fixture
    def tsmixer_onnx(self, tmp_path):
        """Export a small TSMixer to ONNX for testing."""
        model = TSMixer(n_features=17, hidden=32, n_mix_layers=2)
        model.eval()

        # Export via torch
        dummy = torch.randn(1, 336, 17)
        onnx_path = tmp_path / "test_tsmixer.onnx"

        torch.onnx.export(
            model,
            dummy,
            str(onnx_path),
            input_names=["features"],
            output_names=["soh", "rul", "anomaly"],
            dynamic_axes={"features": {0: "batch", 1: "seq_len"}},
            opset_version=17,
        )
        return str(onnx_path)

    def test_inference_output_shape(self, tsmixer_onnx, rng):
        """Inference should return scores for all batteries."""
        runner = InferenceRunner(tsmixer_path=tsmixer_onnx, gnn_path=None)
        features = {
            0: rng.random((336, 17)).astype(np.float32),
            1: rng.random((336, 17)).astype(np.float32),
            2: rng.random((336, 17)).astype(np.float32),
        }
        results = runner.score_batteries(features)

        assert len(results) == 3
        for bat_id, score in results.items():
            assert "soh_score" in score
            assert "rul_days" in score
            assert "anomaly_score" in score
            assert 0.0 <= score["soh_score"] <= 1.0
            assert score["rul_days"] >= 0
            assert 0.0 <= score["anomaly_score"] <= 1.0

    def test_empty_features(self, tsmixer_onnx):
        """Empty feature dict should return empty results."""
        runner = InferenceRunner(tsmixer_path=tsmixer_onnx, gnn_path=None)
        results = runner.score_batteries({})
        assert len(results) == 0
