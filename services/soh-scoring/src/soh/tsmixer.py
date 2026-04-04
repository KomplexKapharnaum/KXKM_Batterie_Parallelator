"""TSMixer-B: Time-Series Mixer for per-battery SOH scoring.

Architecture: 3 mixing layers (time-mixing + feature-mixing MLPs), hidden dim 64.
3-head output: SOH score (sigmoid), RUL days (ReLU), anomaly score (sigmoid).

Reference: Chen et al., "TSMixer: An All-MLP Architecture for Time Series Forecasting" (2023).

Input: (batch, seq_len, n_features) — 7-day feature window per battery
Output: (soh, rul, anomaly) — per-battery scores
"""

from __future__ import annotations

import torch
import torch.nn as nn


class MixingLayer(nn.Module):
    """Single TSMixer mixing layer: time-mixing + feature-mixing MLPs with residuals."""

    def __init__(self, seq_len: int, n_features: int, hidden: int, dropout: float = 0.1):
        super().__init__()
        # Time-mixing: MLP along time axis (applied per feature)
        self.time_norm = nn.LayerNorm(n_features)
        self.time_mlp = nn.Sequential(
            nn.Linear(seq_len, hidden),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hidden, seq_len),
            nn.Dropout(dropout),
        )

        # Feature-mixing: MLP along feature axis (applied per timestep)
        self.feat_norm = nn.LayerNorm(n_features)
        self.feat_mlp = nn.Sequential(
            nn.Linear(n_features, hidden),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hidden, n_features),
            nn.Dropout(dropout),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """x: (batch, seq_len, n_features) -> (batch, seq_len, n_features)"""
        # Time-mixing: transpose to (batch, n_features, seq_len), apply MLP, transpose back
        residual = x
        x_norm = self.time_norm(x)
        x_t = x_norm.transpose(1, 2)  # (batch, n_features, seq_len)
        x_t = self.time_mlp(x_t)
        x = residual + x_t.transpose(1, 2)

        # Feature-mixing: MLP along feature axis
        residual = x
        x_norm = self.feat_norm(x)
        x = residual + self.feat_mlp(x_norm)

        return x


class TSMixer(nn.Module):
    """TSMixer-B with 3-head output for battery SOH scoring.

    Parameters
    ----------
    n_features : int — number of input features (17)
    hidden : int — hidden dimension in mixing MLPs (64)
    n_mix_layers : int — number of mixing layers (3)
    dropout : float — dropout rate (0.1)
    """

    def __init__(
        self,
        n_features: int = 17,
        hidden: int = 64,
        n_mix_layers: int = 3,
        dropout: float = 0.1,
    ):
        super().__init__()
        self.n_features = n_features
        self.hidden = hidden

        # Input projection to hidden dim
        self.input_proj = nn.Linear(n_features, hidden)

        # Mixing layers operate on (batch, seq_len, hidden)
        # Time-mixing MLP needs fixed seq_len — use adaptive pooling instead
        self.mix_layers = nn.ModuleList()
        for _ in range(n_mix_layers):
            self.mix_layers.append(
                _AdaptiveMixingLayer(hidden=hidden, dropout=dropout)
            )

        # Global average pooling over time
        self.pool_norm = nn.LayerNorm(hidden)

        # 3 output heads
        self.soh_head = nn.Sequential(
            nn.Linear(hidden, hidden // 2),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hidden // 2, 1),
            nn.Sigmoid(),
        )

        self.rul_head = nn.Sequential(
            nn.Linear(hidden, hidden // 2),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hidden // 2, 1),
            nn.ReLU(),  # RUL >= 0
        )

        self.anomaly_head = nn.Sequential(
            nn.Linear(hidden, hidden // 2),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hidden // 2, 1),
            nn.Sigmoid(),
        )

    def encode(self, x: torch.Tensor) -> torch.Tensor:
        """Extract hidden representation: (batch, seq_len, n_features) -> (batch, hidden).

        Used by GNN for node embeddings.
        """
        x = self.input_proj(x)  # (batch, seq_len, hidden)
        for layer in self.mix_layers:
            x = layer(x)
        x = self.pool_norm(x)
        return x.mean(dim=1)  # global average pool -> (batch, hidden)

    def forward(self, x: torch.Tensor) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        """Forward pass: (batch, seq_len, n_features) -> (soh, rul, anomaly).

        Returns
        -------
        soh : (batch,) — health score 0-1
        rul : (batch,) — remaining useful life in days (>= 0)
        anomaly : (batch,) — anomaly score 0-1
        """
        h = self.encode(x)  # (batch, hidden)

        soh = self.soh_head(h).squeeze(-1)
        rul = self.rul_head(h).squeeze(-1)
        anomaly = self.anomaly_head(h).squeeze(-1)

        return soh, rul, anomaly


class _AdaptiveMixingLayer(nn.Module):
    """Mixing layer that works with variable sequence lengths.

    Uses 1D convolution for time-mixing instead of fixed-size MLP,
    making the model sequence-length agnostic.
    """

    def __init__(self, hidden: int, dropout: float = 0.1):
        super().__init__()
        # Time-mixing via 1D convolution (kernel=3, causal padding)
        self.time_norm = nn.LayerNorm(hidden)
        self.time_conv = nn.Sequential(
            nn.Conv1d(hidden, hidden, kernel_size=7, padding=3, groups=1),
            nn.ReLU(),
            nn.Dropout(dropout),
        )

        # Feature-mixing: MLP along feature axis
        self.feat_norm = nn.LayerNorm(hidden)
        self.feat_mlp = nn.Sequential(
            nn.Linear(hidden, hidden * 2),
            nn.ReLU(),
            nn.Dropout(dropout),
            nn.Linear(hidden * 2, hidden),
            nn.Dropout(dropout),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """x: (batch, seq_len, hidden) -> (batch, seq_len, hidden)"""
        # Time-mixing via conv1d
        residual = x
        x_norm = self.time_norm(x)
        x_t = x_norm.transpose(1, 2)  # (batch, hidden, seq_len)
        x_t = self.time_conv(x_t)
        x = residual + x_t.transpose(1, 2)

        # Feature-mixing
        residual = x
        x_norm = self.feat_norm(x)
        x = residual + self.feat_mlp(x_norm)

        return x
