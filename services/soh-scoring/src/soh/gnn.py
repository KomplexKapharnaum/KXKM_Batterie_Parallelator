"""Graph Attention Network for fleet-level battery health scoring.

Architecture: GAT (2 layers, hidden 32, 4 attention heads).
Input: node features from TSMixer embeddings + edge features (voltage imbalance, current ratio).
Output: fleet_health, outlier_idx, outlier_score, imbalance_severity.
"""

from __future__ import annotations

import torch
import torch.nn as nn
from torch_geometric.nn import GATv2Conv, global_mean_pool


def build_fleet_graph(
    node_feats: torch.Tensor,
    voltages: torch.Tensor,
    currents: torch.Tensor,
) -> tuple[torch.Tensor, torch.Tensor]:
    """Build a fully-connected graph from battery fleet data.

    Parameters
    ----------
    node_feats : (N, D) — node feature matrix (TSMixer embeddings)
    voltages : (N,) — mean voltage per battery (for edge features)
    currents : (N,) — mean current per battery (for edge features)

    Returns
    -------
    edge_index : (2, N*(N-1)) — COO format edge indices
    edge_attr : (N*(N-1), 2) — edge features: [voltage_imbalance, current_ratio]
    """
    n = node_feats.shape[0]

    # Fully connected graph (no self-loops)
    src, dst = [], []
    for i in range(n):
        for j in range(n):
            if i != j:
                src.append(i)
                dst.append(j)

    edge_index = torch.tensor([src, dst], dtype=torch.long, device=node_feats.device)

    # Edge features
    edge_attr = torch.zeros(len(src), 2, device=node_feats.device)
    for e, (s, d) in enumerate(zip(src, dst)):
        edge_attr[e, 0] = torch.abs(voltages[s] - voltages[d])  # voltage imbalance
        v_max = torch.max(torch.abs(currents[s]), torch.abs(currents[d]))
        v_min = torch.min(torch.abs(currents[s]), torch.abs(currents[d]))
        edge_attr[e, 1] = v_min / (v_max + 1e-6)  # current ratio (0-1, 1=balanced)

    return edge_index, edge_attr


class FleetGNN(nn.Module):
    """Graph Attention Network for fleet-level health scoring.

    Parameters
    ----------
    node_features : int — input feature dim per node (TSMixer hidden dim)
    hidden : int — GAT hidden dimension
    n_layers : int — number of GAT layers
    heads : int — number of attention heads
    """

    def __init__(
        self,
        node_features: int = 64,
        hidden: int = 32,
        n_layers: int = 2,
        heads: int = 4,
        dropout: float = 0.1,
    ):
        super().__init__()
        self.node_features = node_features
        self.hidden = hidden

        # Edge feature projection
        self.edge_proj = nn.Linear(2, hidden)

        # GAT layers
        self.convs = nn.ModuleList()
        self.norms = nn.ModuleList()

        # First layer: node_features -> hidden
        self.convs.append(GATv2Conv(
            node_features, hidden, heads=heads, concat=False,
            edge_dim=hidden, dropout=dropout,
        ))
        self.norms.append(nn.LayerNorm(hidden))

        # Subsequent layers: hidden -> hidden
        for _ in range(n_layers - 1):
            self.convs.append(GATv2Conv(
                hidden, hidden, heads=heads, concat=False,
                edge_dim=hidden, dropout=dropout,
            ))
            self.norms.append(nn.LayerNorm(hidden))

        # Fleet-level head (from graph-level pooled representation)
        self.fleet_head = nn.Sequential(
            nn.Linear(hidden, hidden),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hidden, 1),
            nn.Sigmoid(),
        )

        # Per-node anomaly score (for outlier detection)
        self.node_anomaly_head = nn.Sequential(
            nn.Linear(hidden, hidden // 2),
            nn.ReLU(),
            nn.Linear(hidden // 2, 1),
            nn.Sigmoid(),
        )

        # Imbalance head (from graph-level representation)
        self.imbalance_head = nn.Sequential(
            nn.Linear(hidden, hidden // 2),
            nn.ReLU(),
            nn.Linear(hidden // 2, 1),
            nn.Sigmoid(),
        )

    def forward(
        self,
        x: torch.Tensor,
        edge_index: torch.Tensor,
        edge_attr: torch.Tensor,
    ) -> tuple[torch.Tensor, int, torch.Tensor, torch.Tensor]:
        """Forward pass.

        Parameters
        ----------
        x : (N, node_features) — node features
        edge_index : (2, E) — edge indices
        edge_attr : (E, 2) — edge features

        Returns
        -------
        fleet_health : scalar tensor 0-1
        outlier_idx : int — most anomalous battery index
        outlier_score : scalar tensor 0-1
        imbalance : scalar tensor 0-1
        """
        # Project edge features
        edge_feat = self.edge_proj(edge_attr)

        # GAT convolutions
        for conv, norm in zip(self.convs, self.norms):
            x = conv(x, edge_index, edge_attr=edge_feat)
            x = norm(x)
            x = torch.relu(x)

        # Graph-level pooling (no batch dimension — single graph)
        # Use batch=None for single graph
        batch = torch.zeros(x.shape[0], dtype=torch.long, device=x.device)
        graph_repr = global_mean_pool(x, batch)  # (1, hidden)

        # Fleet health score
        fleet_health = self.fleet_head(graph_repr).squeeze()

        # Per-node anomaly scores for outlier detection
        node_scores = self.node_anomaly_head(x).squeeze(-1)  # (N,)
        outlier_idx = int(node_scores.argmax().item())
        outlier_score = node_scores[outlier_idx]

        # Imbalance severity
        imbalance = self.imbalance_head(graph_repr).squeeze()

        return fleet_health, outlier_idx, outlier_score, imbalance
