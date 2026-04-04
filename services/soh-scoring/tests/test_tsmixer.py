"""Tests for TSMixer model architecture."""

import numpy as np
import pytest
import torch

from soh.tsmixer import TSMixer


class TestTSMixerArchitecture:
    def test_output_shape_batch(self):
        """TSMixer should output 3 heads: soh, rul, anomaly."""
        model = TSMixer(n_features=17, hidden=64, n_mix_layers=3)
        x = torch.randn(4, 336, 17)  # batch=4, seq=336, features=17
        soh, rul, anomaly = model(x)

        assert soh.shape == (4,), f"SOH shape: {soh.shape}"
        assert rul.shape == (4,), f"RUL shape: {rul.shape}"
        assert anomaly.shape == (4,), f"Anomaly shape: {anomaly.shape}"

    def test_soh_range(self):
        """SOH output should be in [0, 1] (sigmoid)."""
        model = TSMixer(n_features=17, hidden=64, n_mix_layers=3)
        x = torch.randn(8, 336, 17)
        soh, _, _ = model(x)

        assert (soh >= 0).all() and (soh <= 1).all(), f"SOH out of range: {soh}"

    def test_anomaly_range(self):
        """Anomaly score should be in [0, 1] (sigmoid)."""
        model = TSMixer(n_features=17, hidden=64, n_mix_layers=3)
        x = torch.randn(8, 336, 17)
        _, _, anomaly = model(x)

        assert (anomaly >= 0).all() and (anomaly <= 1).all()

    def test_rul_non_negative(self):
        """RUL output should be >= 0 (ReLU)."""
        model = TSMixer(n_features=17, hidden=64, n_mix_layers=3)
        x = torch.randn(8, 336, 17)
        _, rul, _ = model(x)

        assert (rul >= 0).all(), f"Negative RUL: {rul}"

    def test_param_count(self):
        """TSMixer with hidden=64, 3 layers should have < 600K params."""
        model = TSMixer(n_features=17, hidden=64, n_mix_layers=3)
        n_params = sum(p.numel() for p in model.parameters())
        assert n_params < 600_000, f"Too many params: {n_params}"
        assert n_params > 10_000, f"Suspiciously few params: {n_params}"

    def test_variable_seq_len(self):
        """TSMixer should handle different sequence lengths."""
        model = TSMixer(n_features=17, hidden=64, n_mix_layers=3)
        for seq_len in [96, 168, 336]:
            x = torch.randn(2, seq_len, 17)
            soh, rul, anomaly = model(x)
            assert soh.shape == (2,)

    def test_single_sample(self):
        """Should work with batch size 1."""
        model = TSMixer(n_features=17, hidden=64, n_mix_layers=3)
        x = torch.randn(1, 336, 17)
        soh, rul, anomaly = model(x)
        assert soh.shape == (1,)

    def test_gradient_flows(self):
        """Verify gradients flow through all 3 heads."""
        model = TSMixer(n_features=17, hidden=64, n_mix_layers=3)
        x = torch.randn(4, 336, 17, requires_grad=True)
        soh, rul, anomaly = model(x)
        loss = soh.sum() + rul.sum() + anomaly.sum()
        loss.backward()
        assert x.grad is not None
        assert x.grad.abs().sum() > 0

    def test_hidden_state_extraction(self):
        """TSMixer should expose last hidden state for GNN embedding."""
        model = TSMixer(n_features=17, hidden=64, n_mix_layers=3)
        x = torch.randn(4, 336, 17)
        hidden = model.encode(x)
        assert hidden.shape == (4, 64), f"Hidden shape: {hidden.shape}"
