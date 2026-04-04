"""Tests for GNN fleet-level scoring model."""

import pytest
import torch

from soh.gnn import FleetGNN, build_fleet_graph


class TestFleetGNN:
    def test_output_shape(self):
        """GNN should output fleet_health, outlier_idx, outlier_score, imbalance."""
        model = FleetGNN(node_features=64, hidden=32, n_layers=2, heads=4)
        # 8 batteries, fully connected
        node_feats = torch.randn(8, 64)
        edge_index, edge_attr = build_fleet_graph(
            node_feats=node_feats,
            voltages=torch.randn(8),
            currents=torch.randn(8),
        )

        fleet_health, outlier_idx, outlier_score, imbalance = model(
            node_feats, edge_index, edge_attr
        )

        assert fleet_health.shape == (), f"fleet_health shape: {fleet_health.shape}"
        assert isinstance(outlier_idx, int)
        assert outlier_score.shape == ()
        assert imbalance.shape == ()

    def test_fleet_health_range(self):
        """fleet_health should be in [0, 1]."""
        model = FleetGNN(node_features=64, hidden=32)
        node_feats = torch.randn(8, 64)
        edge_index, edge_attr = build_fleet_graph(
            node_feats=node_feats,
            voltages=torch.randn(8),
            currents=torch.randn(8),
        )

        fleet_health, _, _, _ = model(node_feats, edge_index, edge_attr)
        assert 0.0 <= fleet_health.item() <= 1.0

    def test_outlier_idx_in_range(self):
        """Outlier index should be a valid battery index."""
        model = FleetGNN(node_features=64, hidden=32)
        n_batteries = 6
        node_feats = torch.randn(n_batteries, 64)
        edge_index, edge_attr = build_fleet_graph(
            node_feats=node_feats,
            voltages=torch.randn(n_batteries),
            currents=torch.randn(n_batteries),
        )

        _, outlier_idx, _, _ = model(node_feats, edge_index, edge_attr)
        assert 0 <= outlier_idx < n_batteries

    def test_variable_fleet_size(self):
        """Should work with different numbers of batteries (2-23)."""
        model = FleetGNN(node_features=64, hidden=32)
        for n in [2, 5, 8, 16, 23]:
            node_feats = torch.randn(n, 64)
            edge_index, edge_attr = build_fleet_graph(
                node_feats=node_feats,
                voltages=torch.randn(n),
                currents=torch.randn(n),
            )
            fleet_health, outlier_idx, _, _ = model(node_feats, edge_index, edge_attr)
            assert 0 <= outlier_idx < n

    def test_param_count(self):
        """GNN should have < 250K params."""
        model = FleetGNN(node_features=64, hidden=32, n_layers=2, heads=4)
        n_params = sum(p.numel() for p in model.parameters())
        assert n_params < 250_000, f"Too many params: {n_params}"

    def test_gradient_flows(self):
        """Gradients should flow through the GNN."""
        model = FleetGNN(node_features=64, hidden=32)
        node_feats = torch.randn(8, 64, requires_grad=True)
        edge_index, edge_attr = build_fleet_graph(
            node_feats=node_feats,
            voltages=torch.randn(8),
            currents=torch.randn(8),
        )

        fleet_health, _, outlier_score, imbalance = model(
            node_feats, edge_index, edge_attr
        )
        loss = fleet_health + outlier_score + imbalance
        loss.backward()
        assert node_feats.grad is not None


class TestBuildFleetGraph:
    def test_fully_connected(self):
        """Graph should be fully connected (N*(N-1) edges for N nodes)."""
        n = 5
        node_feats = torch.randn(n, 64)
        edge_index, edge_attr = build_fleet_graph(
            node_feats=node_feats,
            voltages=torch.randn(n),
            currents=torch.randn(n),
        )

        assert edge_index.shape[0] == 2
        assert edge_index.shape[1] == n * (n - 1)  # fully connected, no self-loops

    def test_edge_features_shape(self):
        """Edge features: voltage imbalance + current ratio = 2 features."""
        n = 4
        node_feats = torch.randn(n, 64)
        edge_index, edge_attr = build_fleet_graph(
            node_feats=node_feats,
            voltages=torch.randn(n),
            currents=torch.randn(n),
        )

        assert edge_attr.shape == (n * (n - 1), 2)
