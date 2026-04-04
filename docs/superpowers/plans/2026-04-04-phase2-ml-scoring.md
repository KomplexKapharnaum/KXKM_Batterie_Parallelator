# Phase 2: ML Scoring Pipeline — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build ETL + TSMixer + GNN scoring pipeline on kxkm-ai with REST API for battery SOH prediction, RUL estimation, and fleet anomaly detection.

**Architecture:** Python services on kxkm-ai. ETL extracts 7-day windows from InfluxDB, TSMixer scores per-battery, GNN scores fleet-level. Results stored in InfluxDB `soh_ml` / `soh_fleet`, served via FastAPI on port 8400.

**Tech Stack:** Python 3.12, PyTorch 2.x, PyTorch Geometric, FastAPI, influxdb-client, ONNX Runtime, Docker Compose

**Infrastructure:**
- Server: kxkm-ai (RTX 4090 24 GB, Tailscale)
- Data source: InfluxDB on kxkm-ai:8086, measurements `rint` + `battery`
- Output: InfluxDB measurements `soh_ml` (per-battery) + `soh_fleet` (fleet-level)
- API: REST on kxkm-ai:8400
- Existing assets: `scripts/ml/` (train_fpnn.py, extract_features.py, train_sambamixer.py), `models/` (fpnn_soh.pt, features.parquet, phase2_metrics.json)

**Baseline metrics (edge FPNN, from phase2_metrics.json):**
- float32 MAPE: 7.73%, quantized MAPE: 10.77%, 6785 params, 16 KB ONNX
- Features: 13 columns (V_mean, V_std, I_mean, I_std, dV_dt, dI_dt, ah_cons, ah_charge, V_min, V_max, I_max, samples, R_internal)

---

## File Structure

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `services/soh-scoring/pyproject.toml` | Python project config, all deps |
| Create | `services/soh-scoring/Dockerfile` | Multi-stage build for scoring + API |
| Create | `services/soh-scoring/docker-compose.yml` | Service definitions: etl, scoring, api |
| Create | `services/soh-scoring/src/soh/__init__.py` | Package init |
| Create | `services/soh-scoring/src/soh/config.py` | Configuration (env vars, defaults) |
| Create | `services/soh-scoring/src/soh/etl.py` | InfluxDB query + feature windowing |
| Create | `services/soh-scoring/src/soh/features.py` | Feature computation (windowed stats) |
| Create | `services/soh-scoring/src/soh/synthetic.py` | Synthetic degradation data generator |
| Create | `services/soh-scoring/src/soh/tsmixer.py` | TSMixer model architecture |
| Create | `services/soh-scoring/src/soh/gnn.py` | GAT model architecture |
| Create | `services/soh-scoring/src/soh/train_tsmixer.py` | TSMixer training script |
| Create | `services/soh-scoring/src/soh/train_gnn.py` | GNN training script |
| Create | `services/soh-scoring/src/soh/export_onnx.py` | ONNX export + quantization |
| Create | `services/soh-scoring/src/soh/inference.py` | Inference runner (ONNX Runtime) |
| Create | `services/soh-scoring/src/soh/scheduler.py` | Cron job: ETL + inference + InfluxDB write |
| Create | `services/soh-scoring/src/soh/api.py` | FastAPI REST endpoints |
| Create | `services/soh-scoring/tests/__init__.py` | Test package |
| Create | `services/soh-scoring/tests/test_features.py` | ETL + feature unit tests |
| Create | `services/soh-scoring/tests/test_tsmixer.py` | TSMixer model tests |
| Create | `services/soh-scoring/tests/test_gnn.py` | GNN model tests |
| Create | `services/soh-scoring/tests/test_inference.py` | Inference pipeline tests |
| Create | `services/soh-scoring/tests/test_api.py` | API integration tests |
| Create | `services/soh-scoring/tests/conftest.py` | Shared fixtures |

---

### Task 1: Project scaffold + configuration

**Files:**
- Create: `services/soh-scoring/pyproject.toml`
- Create: `services/soh-scoring/src/soh/__init__.py`
- Create: `services/soh-scoring/src/soh/config.py`
- Create: `services/soh-scoring/tests/__init__.py`
- Create: `services/soh-scoring/tests/conftest.py`

- [ ] **Step 1: Create pyproject.toml**

Create `services/soh-scoring/pyproject.toml`:
```toml
[project]
name = "kxkm-soh-scoring"
version = "0.1.0"
description = "BMU SOH/RUL scoring pipeline — TSMixer + GNN on kxkm-ai"
requires-python = ">=3.12"
dependencies = [
    "torch>=2.2.0",
    "torch-geometric>=2.5.0",
    "fastapi>=0.115.0",
    "uvicorn[standard]>=0.32.0",
    "influxdb-client>=1.45.0",
    "onnx>=1.16.0",
    "onnxruntime>=1.18.0",
    "numpy>=1.26.0",
    "pandas>=2.2.0",
    "scipy>=1.14.0",
    "pydantic>=2.9.0",
    "pydantic-settings>=2.6.0",
    "apscheduler>=3.10.0",
    "httpx>=0.27.0",
]

[project.optional-dependencies]
dev = [
    "pytest>=8.3.0",
    "pytest-asyncio>=0.24.0",
    "pytest-cov>=5.0.0",
    "ruff>=0.8.0",
]

[tool.pytest.ini_options]
testpaths = ["tests"]
asyncio_mode = "auto"

[tool.ruff]
target-version = "py312"
line-length = 100

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[tool.hatch.build.targets.wheel]
packages = ["src/soh"]

[project.scripts]
soh-api = "soh.api:main"
soh-scheduler = "soh.scheduler:main"
soh-train-tsmixer = "soh.train_tsmixer:main"
soh-train-gnn = "soh.train_gnn:main"
soh-export = "soh.export_onnx:main"
```

- [ ] **Step 2: Create package init**

Create `services/soh-scoring/src/soh/__init__.py`:
```python
"""KXKM BMU SOH/RUL scoring pipeline."""

__version__ = "0.1.0"
```

- [ ] **Step 3: Create configuration module**

Create `services/soh-scoring/src/soh/config.py`:
```python
"""Configuration for SOH scoring pipeline — all settings via env vars."""

from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    """Pipeline configuration. All values overridable via SOH_ prefixed env vars."""

    model_config = {"env_prefix": "SOH_"}

    # InfluxDB
    influxdb_url: str = "http://localhost:8086"
    influxdb_token: str = ""
    influxdb_org: str = "kxkm"
    influxdb_bucket: str = "bmu"
    influxdb_output_bucket: str = "bmu"

    # Feature extraction
    etl_window_days: int = 7
    etl_sample_interval_min: int = 30  # 30-min samples over 7 days = ~336 steps

    # TSMixer
    tsmixer_hidden: int = 64
    tsmixer_n_mix_layers: int = 3
    tsmixer_dropout: float = 0.1
    tsmixer_n_features: int = 17  # ETL output feature count
    tsmixer_seq_len: int = 336  # 7 days / 30 min

    # GNN
    gnn_hidden: int = 32
    gnn_n_layers: int = 2
    gnn_heads: int = 4
    gnn_node_features: int = 64  # TSMixer last hidden dim

    # Inference
    tsmixer_onnx_path: str = "models/tsmixer_soh.onnx"
    gnn_onnx_path: str = "models/gnn_fleet.onnx"
    max_batteries: int = 32

    # Scheduling
    scoring_interval_min: int = 30

    # API
    api_host: str = "0.0.0.0"
    api_port: int = 8400

    # Model paths (PyTorch, for training)
    tsmixer_pt_path: str = "models/tsmixer_soh.pt"
    gnn_pt_path: str = "models/gnn_fleet.pt"


settings = Settings()
```

- [ ] **Step 4: Create test scaffolding**

Create `services/soh-scoring/tests/__init__.py`:
```python
```

Create `services/soh-scoring/tests/conftest.py`:
```python
"""Shared test fixtures for SOH scoring pipeline."""

import numpy as np
import pytest


@pytest.fixture
def rng():
    """Deterministic random generator for reproducible tests."""
    return np.random.default_rng(42)


@pytest.fixture
def sample_feature_matrix(rng):
    """Generate a realistic feature matrix: 8 batteries x 336 time steps x 17 features.

    Feature order matches ETL output:
        r_int_mean, r_int_std, r_int_slope, r_int_max, r_int_min,
        v_mean, v_std, v_min, v_max, v_mean_under_load,
        i_mean, i_std, i_peak, i_duty_cycle,
        ah_discharged, ah_charged, coulombic_efficiency,
    """
    n_batteries = 8
    seq_len = 336
    n_features = 17

    # Realistic ranges for LiFePO4 24-30V batteries
    features = np.zeros((n_batteries, seq_len, n_features), dtype=np.float32)
    for b in range(n_batteries):
        degradation = 1.0 + b * 0.05  # batteries at different health levels
        features[b, :, 0] = rng.normal(15.0 * degradation, 0.5, seq_len)   # r_int_mean (mOhm)
        features[b, :, 1] = rng.normal(0.3, 0.05, seq_len)                 # r_int_std
        features[b, :, 2] = rng.normal(0.08 * degradation, 0.02, seq_len)  # r_int_slope
        features[b, :, 3] = rng.normal(18.0 * degradation, 1.0, seq_len)   # r_int_max
        features[b, :, 4] = rng.normal(12.0 * degradation, 0.8, seq_len)   # r_int_min
        features[b, :, 5] = rng.normal(26500, 200, seq_len)                # v_mean (mV)
        features[b, :, 6] = rng.normal(50, 10, seq_len)                    # v_std
        features[b, :, 7] = rng.normal(24000, 300, seq_len)                # v_min
        features[b, :, 8] = rng.normal(29000, 200, seq_len)                # v_max
        features[b, :, 9] = rng.normal(25800, 250, seq_len)                # v_mean_under_load
        features[b, :, 10] = rng.normal(2.5, 0.8, seq_len)                 # i_mean (A)
        features[b, :, 11] = rng.normal(1.2, 0.3, seq_len)                 # i_std
        features[b, :, 12] = rng.normal(8.0, 2.0, seq_len)                 # i_peak
        features[b, :, 13] = rng.uniform(0.3, 0.8, seq_len)                # i_duty_cycle
        features[b, :, 14] = np.cumsum(rng.uniform(0, 0.1, seq_len))       # ah_discharged
        features[b, :, 15] = np.cumsum(rng.uniform(0, 0.09, seq_len))      # ah_charged
        features[b, :, 16] = rng.normal(0.97, 0.02, seq_len)               # coulombic_efficiency

    return features


@pytest.fixture
def sample_soh_labels(rng):
    """SOH labels for 8 batteries: decreasing health across battery index."""
    n_batteries = 8
    return np.array([0.95 - i * 0.05 for i in range(n_batteries)], dtype=np.float32)


@pytest.fixture
def sample_rul_labels(rng):
    """RUL labels (days) for 8 batteries."""
    return np.array([365, 300, 240, 180, 120, 90, 60, 30], dtype=np.float32)
```

- [ ] **Step 5: Verify scaffold builds**

```bash
cd services/soh-scoring && pip install -e ".[dev]" && pytest --co -q
```

**Commit:** `feat(soh-scoring): project scaffold with pyproject.toml and config`

---

### Task 2: ETL job — InfluxDB query to feature windows

**Files:**
- Create: `services/soh-scoring/src/soh/etl.py`
- Create: `services/soh-scoring/src/soh/features.py`
- Create: `services/soh-scoring/tests/test_features.py`

- [ ] **Step 1: Write failing tests for feature computation**

Create `services/soh-scoring/tests/test_features.py`:
```python
"""Tests for ETL feature extraction pipeline."""

import numpy as np
import pytest

from soh.features import compute_battery_features, FeatureWindow, FEATURE_NAMES


class TestFeatureNames:
    def test_feature_count(self):
        assert len(FEATURE_NAMES) == 17

    def test_expected_features_present(self):
        expected = [
            "r_int_mean", "r_int_std", "r_int_slope",
            "v_mean", "v_std", "v_min", "v_max",
            "i_mean", "i_std", "i_peak", "i_duty_cycle",
            "ah_discharged", "ah_charged", "coulombic_efficiency",
        ]
        for name in expected:
            assert name in FEATURE_NAMES, f"Missing feature: {name}"


class TestComputeBatteryFeatures:
    def test_output_shape(self, rng):
        """7 days of 1-min data → 336 time steps (30-min aggregation)."""
        n_points = 7 * 24 * 60  # 1-min data for 7 days
        timestamps = np.arange(n_points) * 60  # seconds
        voltages = rng.normal(26500, 200, n_points).astype(np.float32)
        currents = rng.normal(2.5, 1.0, n_points).astype(np.float32)
        r_int = rng.normal(15.0, 0.5, n_points).astype(np.float32)
        die_temp = rng.normal(35.0, 2.0, n_points).astype(np.float32)

        result = compute_battery_features(
            timestamps_s=timestamps,
            voltages_mv=voltages,
            currents_a=currents,
            r_int_mohm=r_int,
            die_temp_c=die_temp,
            window_min=30,
        )

        assert isinstance(result, FeatureWindow)
        # 7 days * 24 hours * 2 windows/hour = 336
        assert result.matrix.shape == (336, 17)
        assert not np.any(np.isnan(result.matrix)), "No NaN in healthy data"

    def test_r_int_slope_positive_for_degrading(self, rng):
        """R_int slope should be positive when resistance is trending up."""
        n_points = 7 * 24 * 60
        timestamps = np.arange(n_points) * 60
        voltages = np.full(n_points, 26500, dtype=np.float32)
        currents = np.full(n_points, 2.0, dtype=np.float32)
        # Linearly increasing R_int: clear degradation
        r_int = np.linspace(10.0, 20.0, n_points).astype(np.float32)
        die_temp = np.full(n_points, 35.0, dtype=np.float32)

        result = compute_battery_features(
            timestamps_s=timestamps,
            voltages_mv=voltages,
            currents_a=currents,
            r_int_mohm=r_int,
            die_temp_c=die_temp,
            window_min=30,
        )

        # r_int_slope is feature index 2
        slopes = result.matrix[:, 2]
        assert np.mean(slopes) > 0, "Mean R_int slope should be positive for degrading battery"

    def test_duty_cycle_range(self, rng):
        """i_duty_cycle must be in [0, 1]."""
        n_points = 7 * 24 * 60
        timestamps = np.arange(n_points) * 60
        voltages = rng.normal(26500, 200, n_points).astype(np.float32)
        currents = rng.normal(2.5, 1.0, n_points).astype(np.float32)
        r_int = rng.normal(15.0, 0.5, n_points).astype(np.float32)
        die_temp = rng.normal(35.0, 2.0, n_points).astype(np.float32)

        result = compute_battery_features(
            timestamps_s=timestamps,
            voltages_mv=voltages,
            currents_a=currents,
            r_int_mohm=r_int,
            die_temp_c=die_temp,
            window_min=30,
        )

        duty = result.matrix[:, 13]  # i_duty_cycle
        assert np.all(duty >= 0.0) and np.all(duty <= 1.0)

    def test_short_data_returns_fewer_windows(self, rng):
        """Less than 7 days of data should still work with fewer windows."""
        n_points = 2 * 24 * 60  # 2 days
        timestamps = np.arange(n_points) * 60
        voltages = rng.normal(26500, 200, n_points).astype(np.float32)
        currents = rng.normal(2.5, 1.0, n_points).astype(np.float32)
        r_int = rng.normal(15.0, 0.5, n_points).astype(np.float32)
        die_temp = rng.normal(35.0, 2.0, n_points).astype(np.float32)

        result = compute_battery_features(
            timestamps_s=timestamps,
            voltages_mv=voltages,
            currents_a=currents,
            r_int_mohm=r_int,
            die_temp_c=die_temp,
            window_min=30,
        )

        expected_windows = 2 * 24 * 2  # 96
        assert result.matrix.shape[0] == expected_windows
        assert result.matrix.shape[1] == 17

    def test_empty_data_raises(self):
        """Empty arrays should raise ValueError."""
        with pytest.raises(ValueError, match="at least"):
            compute_battery_features(
                timestamps_s=np.array([], dtype=np.float32),
                voltages_mv=np.array([], dtype=np.float32),
                currents_a=np.array([], dtype=np.float32),
                r_int_mohm=np.array([], dtype=np.float32),
                die_temp_c=np.array([], dtype=np.float32),
                window_min=30,
            )

    def test_coulombic_efficiency_near_one(self, rng):
        """With balanced charge/discharge, coulombic efficiency ~ 1.0."""
        n_points = 7 * 24 * 60
        timestamps = np.arange(n_points) * 60
        voltages = rng.normal(26500, 200, n_points).astype(np.float32)
        # Symmetric charge/discharge pattern
        currents = np.sin(np.linspace(0, 100 * np.pi, n_points)).astype(np.float32) * 5.0
        r_int = rng.normal(15.0, 0.5, n_points).astype(np.float32)
        die_temp = rng.normal(35.0, 2.0, n_points).astype(np.float32)

        result = compute_battery_features(
            timestamps_s=timestamps,
            voltages_mv=voltages,
            currents_a=currents,
            r_int_mohm=r_int,
            die_temp_c=die_temp,
            window_min=30,
        )

        ce = result.matrix[:, 16]  # coulombic_efficiency
        valid = ce[~np.isnan(ce)]
        if len(valid) > 0:
            assert np.mean(valid) > 0.8, "Symmetric charge/discharge should yield CE near 1.0"
```

- [ ] **Step 2: Implement feature computation**

Create `services/soh-scoring/src/soh/features.py`:
```python
"""Feature computation for battery SOH scoring.

Computes 17 windowed features per battery from raw InfluxDB time series:
    R_int statistics:    r_int_mean, r_int_std, r_int_slope, r_int_max, r_int_min
    Voltage statistics:  v_mean, v_std, v_min, v_max, v_mean_under_load
    Current statistics:  i_mean, i_std, i_peak, i_duty_cycle
    Energy:              ah_discharged, ah_charged, coulombic_efficiency

Each feature is computed over 30-minute non-overlapping windows.
"""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np
from scipy import stats

FEATURE_NAMES: list[str] = [
    "r_int_mean",
    "r_int_std",
    "r_int_slope",
    "r_int_max",
    "r_int_min",
    "v_mean",
    "v_std",
    "v_min",
    "v_max",
    "v_mean_under_load",
    "i_mean",
    "i_std",
    "i_peak",
    "i_duty_cycle",
    "ah_discharged",
    "ah_charged",
    "coulombic_efficiency",
]

# Threshold for "under load" current (A)
LOAD_CURRENT_THRESHOLD = 0.5


@dataclass
class FeatureWindow:
    """Result of feature extraction for a single battery."""

    matrix: np.ndarray  # shape (n_windows, 17)
    window_timestamps: np.ndarray  # shape (n_windows,) — start timestamp of each window in seconds
    battery_id: int = -1


def compute_battery_features(
    timestamps_s: np.ndarray,
    voltages_mv: np.ndarray,
    currents_a: np.ndarray,
    r_int_mohm: np.ndarray,
    die_temp_c: np.ndarray,
    window_min: int = 30,
) -> FeatureWindow:
    """Compute windowed features from raw battery time series.

    Parameters
    ----------
    timestamps_s : array of float — sample timestamps in seconds since epoch
    voltages_mv : array of float — voltage in millivolts
    currents_a : array of float — current in amps
    r_int_mohm : array of float — internal resistance in milliohms
    die_temp_c : array of float — die temperature in Celsius
    window_min : int — window duration in minutes (default 30)

    Returns
    -------
    FeatureWindow with matrix shape (n_windows, 17)
    """
    if len(timestamps_s) < 2:
        raise ValueError("Need at least 2 data points for feature extraction")

    window_s = window_min * 60
    t0 = timestamps_s[0]
    t_rel = timestamps_s - t0
    total_duration = t_rel[-1]
    n_windows = int(total_duration // window_s)

    if n_windows < 1:
        raise ValueError(f"Data span ({total_duration:.0f}s) shorter than one window ({window_s}s)")

    features = np.zeros((n_windows, len(FEATURE_NAMES)), dtype=np.float32)
    window_ts = np.zeros(n_windows, dtype=np.float64)

    for w in range(n_windows):
        w_start = w * window_s
        w_end = (w + 1) * window_s
        mask = (t_rel >= w_start) & (t_rel < w_end)

        if mask.sum() < 2:
            features[w, :] = np.nan
            window_ts[w] = t0 + w_start
            continue

        v = voltages_mv[mask]
        i = currents_a[mask]
        r = r_int_mohm[mask]
        t_w = t_rel[mask]

        window_ts[w] = t0 + w_start

        # R_int statistics
        features[w, 0] = np.nanmean(r)  # r_int_mean
        features[w, 1] = np.nanstd(r)   # r_int_std
        # r_int_slope: linear regression slope (mOhm per window)
        if len(r) > 2:
            slope_result = stats.linregress(t_w - t_w[0], r)
            features[w, 2] = slope_result.slope * window_s  # normalize to per-window
        else:
            features[w, 2] = 0.0
        features[w, 3] = np.nanmax(r)   # r_int_max
        features[w, 4] = np.nanmin(r)   # r_int_min

        # Voltage statistics
        features[w, 5] = np.nanmean(v)  # v_mean
        features[w, 6] = np.nanstd(v)   # v_std
        features[w, 7] = np.nanmin(v)   # v_min
        features[w, 8] = np.nanmax(v)   # v_max

        # v_mean_under_load: mean voltage when |I| > threshold
        load_mask = np.abs(i) > LOAD_CURRENT_THRESHOLD
        if load_mask.any():
            features[w, 9] = np.nanmean(v[load_mask])
        else:
            features[w, 9] = features[w, 5]  # fallback to v_mean

        # Current statistics
        features[w, 10] = np.nanmean(i)           # i_mean
        features[w, 11] = np.nanstd(i)            # i_std
        features[w, 12] = np.nanmax(np.abs(i))    # i_peak
        features[w, 13] = float(load_mask.sum()) / len(i)  # i_duty_cycle

        # Energy: trapezoidal integration of |I| over time
        dt = np.diff(t_w) / 3600.0  # hours
        i_mid = (np.abs(i[:-1]) + np.abs(i[1:])) / 2.0
        discharge_mask = i[:-1] > LOAD_CURRENT_THRESHOLD
        charge_mask = i[:-1] < -LOAD_CURRENT_THRESHOLD

        features[w, 14] = np.sum(i_mid[discharge_mask] * dt[discharge_mask]) if discharge_mask.any() else 0.0  # ah_discharged
        features[w, 15] = np.sum(i_mid[charge_mask] * dt[charge_mask]) if charge_mask.any() else 0.0  # ah_charged

        # Coulombic efficiency
        if features[w, 14] > 0.01:
            features[w, 16] = min(features[w, 15] / features[w, 14], 2.0)
        else:
            features[w, 16] = np.nan

    return FeatureWindow(matrix=features, window_timestamps=window_ts)
```

- [ ] **Step 3: Implement InfluxDB ETL module**

Create `services/soh-scoring/src/soh/etl.py`:
```python
"""ETL: InfluxDB → feature windows per battery.

Queries InfluxDB for the last N days of `rint` and `battery` measurements,
computes windowed features per battery, and returns a dict of FeatureWindow objects.
"""

from __future__ import annotations

import logging
from datetime import datetime, timezone

import numpy as np
from influxdb_client import InfluxDBClient

from soh.config import settings
from soh.features import FeatureWindow, compute_battery_features

logger = logging.getLogger(__name__)


def query_battery_data(
    battery_id: int,
    days: int = 7,
    client: InfluxDBClient | None = None,
) -> dict[str, np.ndarray]:
    """Query InfluxDB for raw battery + rint data.

    Returns dict with keys: timestamps_s, voltages_mv, currents_a, r_int_mohm, die_temp_c
    """
    own_client = client is None
    if own_client:
        client = InfluxDBClient(
            url=settings.influxdb_url,
            token=settings.influxdb_token,
            org=settings.influxdb_org,
        )

    try:
        query_api = client.query_api()

        # Query battery voltage/current data
        flux_battery = f'''
        from(bucket: "{settings.influxdb_bucket}")
          |> range(start: -{days}d)
          |> filter(fn: (r) => r._measurement == "battery")
          |> filter(fn: (r) => r.battery == "{battery_id}")
          |> filter(fn: (r) => r._field == "voltage" or r._field == "current" or r._field == "die_temp")
          |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
          |> sort(columns: ["_time"])
        '''

        # Query R_int data
        flux_rint = f'''
        from(bucket: "{settings.influxdb_bucket}")
          |> range(start: -{days}d)
          |> filter(fn: (r) => r._measurement == "rint")
          |> filter(fn: (r) => r.battery == "{battery_id}")
          |> filter(fn: (r) => r._field == "r_ohmic_mohm")
          |> sort(columns: ["_time"])
        '''

        # Parse battery data
        tables_bat = query_api.query(flux_battery)
        timestamps, voltages, currents, temps = [], [], [], []
        for table in tables_bat:
            for record in table.records:
                ts = record.get_time().timestamp()
                timestamps.append(ts)
                voltages.append(record.values.get("voltage", 0.0))
                currents.append(record.values.get("current", 0.0))
                temps.append(record.values.get("die_temp", 35.0))

        # Parse R_int data — interpolate to battery timestamps
        tables_rint = query_api.query(flux_rint)
        rint_ts, rint_vals = [], []
        for table in tables_rint:
            for record in table.records:
                rint_ts.append(record.get_time().timestamp())
                rint_vals.append(record.get_value())

        # Interpolate R_int to battery timestamps
        if rint_ts and timestamps:
            r_int_interp = np.interp(timestamps, rint_ts, rint_vals)
        else:
            r_int_interp = np.full(len(timestamps), np.nan)

        return {
            "timestamps_s": np.array(timestamps, dtype=np.float64),
            "voltages_mv": np.array(voltages, dtype=np.float32),
            "currents_a": np.array(currents, dtype=np.float32),
            "r_int_mohm": r_int_interp.astype(np.float32),
            "die_temp_c": np.array(temps, dtype=np.float32),
        }
    finally:
        if own_client:
            client.close()


def extract_all_batteries(
    n_batteries: int | None = None,
    days: int | None = None,
) -> dict[int, FeatureWindow]:
    """Run full ETL: query InfluxDB → compute features for all batteries.

    Returns dict mapping battery_id → FeatureWindow.
    """
    days = days or settings.etl_window_days
    n_batteries = n_batteries or settings.max_batteries

    client = InfluxDBClient(
        url=settings.influxdb_url,
        token=settings.influxdb_token,
        org=settings.influxdb_org,
    )

    results: dict[int, FeatureWindow] = {}
    try:
        for bat_id in range(n_batteries):
            try:
                data = query_battery_data(bat_id, days=days, client=client)
                if len(data["timestamps_s"]) < 60:  # need at least 1 hour of data
                    logger.warning("Battery %d: insufficient data (%d points), skipping",
                                   bat_id, len(data["timestamps_s"]))
                    continue

                fw = compute_battery_features(
                    timestamps_s=data["timestamps_s"],
                    voltages_mv=data["voltages_mv"],
                    currents_a=data["currents_a"],
                    r_int_mohm=data["r_int_mohm"],
                    die_temp_c=data["die_temp_c"],
                    window_min=settings.etl_sample_interval_min,
                )
                fw.battery_id = bat_id
                results[bat_id] = fw
                logger.info("Battery %d: %d windows extracted", bat_id, fw.matrix.shape[0])
            except Exception:
                logger.exception("Battery %d: ETL failed", bat_id)
    finally:
        client.close()

    logger.info("ETL complete: %d batteries with features", len(results))
    return results
```

- [ ] **Step 4: Run tests**

```bash
cd services/soh-scoring && uv run pytest tests/test_features.py -v
```

**Commit:** `feat(soh-scoring): ETL pipeline — InfluxDB query + 17-feature windowing`

---

### Task 3: Synthetic data generator

**Files:**
- Create: `services/soh-scoring/src/soh/synthetic.py`

- [ ] **Step 1: Implement synthetic degradation profiles**

Create `services/soh-scoring/src/soh/synthetic.py`:
```python
"""Synthetic battery degradation data generator for training.

Generates realistic LiFePO4/Li-ion degradation profiles with four scenarios:
1. Calendar aging: R_int linear drift +0.5–2 mOhm/month
2. Cycle aging: R_int exponential rise after knee point
3. Sudden failure: step-change in R_int (connection degradation)
4. Temperature stress: accelerated R_int rise at high temperature

Each profile generates:
- Feature matrix: (seq_len, 17) matching ETL feature format
- Labels: soh_score (0–1), rul_days (int), anomaly_score (0–1)
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

import numpy as np


class DegradationMode(Enum):
    CALENDAR = "calendar"
    CYCLE = "cycle"
    SUDDEN_FAILURE = "sudden_failure"
    TEMPERATURE_STRESS = "temperature_stress"
    HEALTHY = "healthy"


@dataclass
class SyntheticProfile:
    """A single synthetic battery degradation profile."""
    features: np.ndarray       # (seq_len, 17)
    soh_score: float           # 0.0–1.0
    rul_days: float            # estimated remaining useful life
    anomaly_score: float       # 0.0–1.0
    mode: DegradationMode
    battery_id: int = 0


def generate_profile(
    mode: DegradationMode,
    seq_len: int = 336,
    rng: np.random.Generator | None = None,
    severity: float | None = None,
) -> SyntheticProfile:
    """Generate a single synthetic degradation profile.

    Parameters
    ----------
    mode : DegradationMode
    seq_len : int — number of 30-min windows (336 = 7 days)
    rng : numpy random generator
    severity : float 0–1 — how degraded the battery is (None = random)
    """
    if rng is None:
        rng = np.random.default_rng()
    if severity is None:
        severity = rng.uniform(0.0, 1.0)

    features = np.zeros((seq_len, 17), dtype=np.float32)
    t = np.linspace(0, 1, seq_len)  # normalized time

    # Base healthy battery parameters (LiFePO4 24-30V)
    r_int_base = rng.uniform(10.0, 16.0)  # mOhm
    v_nominal = rng.uniform(25500, 27500)  # mV
    i_nominal = rng.uniform(1.5, 4.0)     # A

    if mode == DegradationMode.HEALTHY:
        r_int = r_int_base + rng.normal(0, 0.1, seq_len)
        soh = rng.uniform(0.90, 1.0)
        rul = rng.uniform(500, 2000)
        anomaly = rng.uniform(0.0, 0.05)

    elif mode == DegradationMode.CALENDAR:
        # Linear R_int drift: +0.5–2 mOhm/month → over 7 days, small drift
        drift_rate = severity * 2.0  # mOhm/month
        monthly_fraction = 7.0 / 30.0
        r_int = r_int_base + drift_rate * monthly_fraction * t + rng.normal(0, 0.15, seq_len)
        soh = max(0.0, 1.0 - severity * 0.4)
        rul = max(10, (1.0 - severity) * 1500)
        anomaly = severity * 0.15

    elif mode == DegradationMode.CYCLE:
        # Exponential knee: R_int stable then accelerating rise
        knee = rng.uniform(0.3, 0.7)
        r_int = r_int_base + np.where(
            t < knee,
            severity * 0.5 * t,
            severity * 0.5 * knee + severity * 5.0 * (t - knee) ** 2,
        ) + rng.normal(0, 0.2, seq_len)
        soh = max(0.0, 1.0 - severity * 0.5)
        rul = max(5, (1.0 - severity) * 800)
        anomaly = severity * 0.3

    elif mode == DegradationMode.SUDDEN_FAILURE:
        # Step change in R_int at random point
        failure_point = int(rng.uniform(0.2, 0.8) * seq_len)
        r_int = np.full(seq_len, r_int_base, dtype=np.float32)
        step_magnitude = severity * 15.0  # up to 15 mOhm jump
        r_int[failure_point:] += step_magnitude
        r_int += rng.normal(0, 0.3, seq_len)
        soh = max(0.0, 1.0 - severity * 0.6)
        rul = max(1, (1.0 - severity) * 200)
        anomaly = min(1.0, severity * 0.8 + 0.2)

    elif mode == DegradationMode.TEMPERATURE_STRESS:
        # Accelerated degradation from temperature
        temp_factor = 1.0 + severity * 2.0
        r_int = r_int_base * (1.0 + 0.3 * severity * t * temp_factor) + rng.normal(0, 0.2, seq_len)
        soh = max(0.0, 1.0 - severity * 0.35)
        rul = max(20, (1.0 - severity) * 1000)
        anomaly = severity * 0.25

    else:
        raise ValueError(f"Unknown degradation mode: {mode}")

    # Fill feature columns
    features[:, 0] = r_int                              # r_int_mean (per window, ~= sample)
    features[:, 1] = np.abs(rng.normal(0.2, 0.05, seq_len))  # r_int_std
    features[:, 2] = np.gradient(r_int)                  # r_int_slope (approximate)
    features[:, 3] = r_int + rng.uniform(0.5, 2.0, seq_len)  # r_int_max
    features[:, 4] = r_int - rng.uniform(0.5, 2.0, seq_len)  # r_int_min

    # Voltage: degrades with SOH
    v_sag = (1.0 - soh) * 2000  # mV sag for degraded batteries
    features[:, 5] = v_nominal - v_sag + rng.normal(0, 100, seq_len)   # v_mean
    features[:, 6] = rng.uniform(30, 80, seq_len)                       # v_std
    features[:, 7] = features[:, 5] - rng.uniform(500, 2500, seq_len)   # v_min
    features[:, 8] = features[:, 5] + rng.uniform(200, 1500, seq_len)   # v_max
    features[:, 9] = features[:, 5] - rng.uniform(100, 800, seq_len)    # v_mean_under_load

    # Current
    features[:, 10] = i_nominal + rng.normal(0, 0.3, seq_len)           # i_mean
    features[:, 11] = rng.uniform(0.5, 2.0, seq_len)                    # i_std
    features[:, 12] = i_nominal * rng.uniform(2.0, 4.0, seq_len)        # i_peak
    features[:, 13] = rng.uniform(0.3, 0.8, seq_len)                    # i_duty_cycle

    # Energy
    features[:, 14] = np.cumsum(rng.uniform(0, 0.15, seq_len))          # ah_discharged
    features[:, 15] = np.cumsum(rng.uniform(0, 0.14, seq_len))          # ah_charged
    ce_base = 0.98 - severity * 0.15
    features[:, 16] = rng.normal(ce_base, 0.02, seq_len).clip(0.5, 1.1) # coulombic_efficiency

    return SyntheticProfile(
        features=features,
        soh_score=float(np.clip(soh, 0.0, 1.0)),
        rul_days=float(max(0, rul)),
        anomaly_score=float(np.clip(anomaly, 0.0, 1.0)),
        mode=mode,
    )


def generate_dataset(
    n_samples: int = 5000,
    seq_len: int = 336,
    seed: int = 42,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Generate full synthetic training dataset.

    Returns
    -------
    X : (n_samples, seq_len, 17) — feature matrices
    y_soh : (n_samples,) — SOH scores 0–1
    y_rul : (n_samples,) — RUL in days
    y_anomaly : (n_samples,) — anomaly scores 0–1
    """
    rng = np.random.default_rng(seed)

    modes = list(DegradationMode)
    # Distribution: 30% healthy, 25% calendar, 20% cycle, 15% sudden, 10% temp
    weights = [0.30, 0.25, 0.20, 0.15, 0.10]

    X = np.zeros((n_samples, seq_len, 17), dtype=np.float32)
    y_soh = np.zeros(n_samples, dtype=np.float32)
    y_rul = np.zeros(n_samples, dtype=np.float32)
    y_anomaly = np.zeros(n_samples, dtype=np.float32)

    for i in range(n_samples):
        mode = rng.choice(modes, p=weights)
        profile = generate_profile(mode, seq_len=seq_len, rng=rng)
        X[i] = profile.features
        y_soh[i] = profile.soh_score
        y_rul[i] = profile.rul_days
        y_anomaly[i] = profile.anomaly_score

    return X, y_soh, y_rul, y_anomaly
```

- [ ] **Step 2: Add synthetic data tests to conftest or inline**

Verify synthetic data generator produces valid outputs:
```bash
cd services/soh-scoring && uv run python -c "
from soh.synthetic import generate_dataset
X, y_soh, y_rul, y_anomaly = generate_dataset(n_samples=100, seq_len=336)
print(f'X: {X.shape}, soh: {y_soh.shape}, rul: {y_rul.shape}, anomaly: {y_anomaly.shape}')
assert X.shape == (100, 336, 17)
assert (y_soh >= 0).all() and (y_soh <= 1).all()
assert (y_rul >= 0).all()
assert (y_anomaly >= 0).all() and (y_anomaly <= 1).all()
print('OK')
"
```

**Commit:** `feat(soh-scoring): synthetic LiFePO4/Li-ion degradation data generator`

---

### Task 4: TSMixer model — architecture + training

**Files:**
- Create: `services/soh-scoring/src/soh/tsmixer.py`
- Create: `services/soh-scoring/src/soh/train_tsmixer.py`
- Create: `services/soh-scoring/tests/test_tsmixer.py`

- [ ] **Step 1: Write failing tests for TSMixer**

Create `services/soh-scoring/tests/test_tsmixer.py`:
```python
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
```

- [ ] **Step 2: Implement TSMixer architecture**

Create `services/soh-scoring/src/soh/tsmixer.py`:
```python
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
        soh : (batch,) — health score 0–1
        rul : (batch,) — remaining useful life in days (>= 0)
        anomaly : (batch,) — anomaly score 0–1
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
```

- [ ] **Step 3: Implement TSMixer training script**

Create `services/soh-scoring/src/soh/train_tsmixer.py`:
```python
"""Training script for TSMixer SOH model.

Uses synthetic data + optional real InfluxDB data for training.
Supports multi-head loss: SOH (MSE) + RUL (Huber) + Anomaly (BCE).
"""

from __future__ import annotations

import argparse
import logging
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

from soh.config import settings
from soh.synthetic import generate_dataset
from soh.tsmixer import TSMixer

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)-8s %(message)s")
logger = logging.getLogger("train_tsmixer")


def train_tsmixer(
    output_dir: Path,
    n_synthetic: int = 5000,
    epochs: int = 50,
    batch_size: int = 32,
    lr: float = 1e-3,
    hidden: int = 64,
    n_mix_layers: int = 3,
    seed: int = 42,
) -> dict:
    """Train TSMixer model on synthetic + optional real data.

    Returns dict with training metrics.
    """
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    logger.info("Device: %s", device)

    # Generate synthetic training data
    logger.info("Generating %d synthetic profiles...", n_synthetic)
    X, y_soh, y_rul, y_anomaly = generate_dataset(
        n_samples=n_synthetic,
        seq_len=settings.tsmixer_seq_len,
        seed=seed,
    )

    # Normalize RUL to [0, 1] range for balanced loss
    rul_max = y_rul.max()
    if rul_max > 0:
        y_rul_norm = y_rul / rul_max
    else:
        y_rul_norm = y_rul

    # Train/val split (80/20)
    rng = np.random.default_rng(seed)
    idx = rng.permutation(len(X))
    split = int(0.8 * len(X))
    train_idx, val_idx = idx[:split], idx[split:]

    X_train = torch.from_numpy(X[train_idx])
    X_val = torch.from_numpy(X[val_idx])
    y_soh_train = torch.from_numpy(y_soh[train_idx])
    y_soh_val = torch.from_numpy(y_soh[val_idx])
    y_rul_train = torch.from_numpy(y_rul_norm[train_idx])
    y_rul_val = torch.from_numpy(y_rul_norm[val_idx])
    y_anomaly_train = torch.from_numpy(y_anomaly[train_idx])
    y_anomaly_val = torch.from_numpy(y_anomaly[val_idx])

    train_ds = TensorDataset(X_train, y_soh_train, y_rul_train, y_anomaly_train)
    val_ds = TensorDataset(X_val, y_soh_val, y_rul_val, y_anomaly_val)
    train_loader = DataLoader(train_ds, batch_size=batch_size, shuffle=True, drop_last=True)
    val_loader = DataLoader(val_ds, batch_size=batch_size * 2, shuffle=False)

    # Model
    model = TSMixer(
        n_features=settings.tsmixer_n_features,
        hidden=hidden,
        n_mix_layers=n_mix_layers,
    ).to(device)

    n_params = sum(p.numel() for p in model.parameters())
    logger.info("TSMixer: hidden=%d, layers=%d, params=%d", hidden, n_mix_layers, n_params)

    # Loss functions: weighted multi-head
    criterion_soh = nn.MSELoss()
    criterion_rul = nn.HuberLoss(delta=0.1)
    criterion_anomaly = nn.BCELoss()
    optimizer = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=epochs)

    # Loss weights
    w_soh, w_rul, w_anomaly = 1.0, 0.5, 0.3

    best_val_loss = float("inf")
    best_epoch = -1
    best_state = None
    patience = 10
    patience_counter = 0
    t0 = time.time()

    for epoch in range(1, epochs + 1):
        # Train
        model.train()
        train_loss_sum = 0.0
        train_n = 0
        for xb, ys, yr, ya in train_loader:
            xb = xb.to(device)
            ys, yr, ya = ys.to(device), yr.to(device), ya.to(device)

            soh_pred, rul_pred, anom_pred = model(xb)
            loss = (
                w_soh * criterion_soh(soh_pred, ys)
                + w_rul * criterion_rul(rul_pred, yr)
                + w_anomaly * criterion_anomaly(anom_pred, ya)
            )

            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()

            train_loss_sum += loss.item() * len(xb)
            train_n += len(xb)

        scheduler.step()
        train_loss = train_loss_sum / train_n

        # Validate
        model.eval()
        val_loss_sum = 0.0
        val_n = 0
        with torch.no_grad():
            for xb, ys, yr, ya in val_loader:
                xb = xb.to(device)
                ys, yr, ya = ys.to(device), yr.to(device), ya.to(device)

                soh_pred, rul_pred, anom_pred = model(xb)
                loss = (
                    w_soh * criterion_soh(soh_pred, ys)
                    + w_rul * criterion_rul(rul_pred, yr)
                    + w_anomaly * criterion_anomaly(anom_pred, ya)
                )
                val_loss_sum += loss.item() * len(xb)
                val_n += len(xb)

        val_loss = val_loss_sum / val_n

        if epoch == 1 or epoch % 10 == 0 or epoch == epochs:
            logger.info("Epoch %3d/%d  train=%.6f  val=%.6f  (%.1fs)",
                        epoch, epochs, train_loss, val_loss, time.time() - t0)

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_epoch = epoch
            patience_counter = 0
            best_state = {k: v.cpu().clone() for k, v in model.state_dict().items()}
        else:
            patience_counter += 1
            if patience_counter >= patience:
                logger.info("Early stopping at epoch %d (best: %d)", epoch, best_epoch)
                break

    # Restore best
    if best_state:
        model.load_state_dict(best_state)
        model.to(device)

    # Save
    output_dir.mkdir(parents=True, exist_ok=True)
    pt_path = output_dir / "tsmixer_soh.pt"
    torch.save({
        "model_state_dict": model.state_dict(),
        "n_features": settings.tsmixer_n_features,
        "hidden": hidden,
        "n_mix_layers": n_mix_layers,
        "best_epoch": best_epoch,
        "best_val_loss": best_val_loss,
        "rul_max": float(rul_max),
        "n_params": n_params,
    }, pt_path)

    logger.info("Saved TSMixer: %s (%.1f KB, %d params, best epoch %d)",
                pt_path, pt_path.stat().st_size / 1024, n_params, best_epoch)

    return {
        "n_params": n_params,
        "best_epoch": best_epoch,
        "best_val_loss": float(best_val_loss),
        "rul_max": float(rul_max),
        "model_path": str(pt_path),
    }


def main():
    parser = argparse.ArgumentParser(description="Train TSMixer for battery SOH scoring")
    parser.add_argument("--output-dir", type=Path, default=Path("models"))
    parser.add_argument("--n-synthetic", type=int, default=5000)
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--hidden", type=int, default=64)
    parser.add_argument("--n-mix-layers", type=int, default=3)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    train_tsmixer(
        output_dir=args.output_dir,
        n_synthetic=args.n_synthetic,
        epochs=args.epochs,
        batch_size=args.batch_size,
        lr=args.lr,
        hidden=args.hidden,
        n_mix_layers=args.n_mix_layers,
        seed=args.seed,
    )


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run TSMixer tests**

```bash
cd services/soh-scoring && uv run pytest tests/test_tsmixer.py -v
```

**Commit:** `feat(soh-scoring): TSMixer 3-head model (SOH + RUL + anomaly) with training`

---

### Task 5: GNN model — GAT fleet scoring

**Files:**
- Create: `services/soh-scoring/src/soh/gnn.py`
- Create: `services/soh-scoring/src/soh/train_gnn.py`
- Create: `services/soh-scoring/tests/test_gnn.py`

- [ ] **Step 1: Write failing GNN tests**

Create `services/soh-scoring/tests/test_gnn.py`:
```python
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
        """Should work with different numbers of batteries (2–23)."""
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
```

- [ ] **Step 2: Implement FleetGNN**

Create `services/soh-scoring/src/soh/gnn.py`:
```python
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
        edge_attr[e, 1] = v_min / (v_max + 1e-6)  # current ratio (0–1, 1=balanced)

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
        fleet_health : scalar tensor 0–1
        outlier_idx : int — most anomalous battery index
        outlier_score : scalar tensor 0–1
        imbalance : scalar tensor 0–1
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
```

- [ ] **Step 3: Implement GNN training script**

Create `services/soh-scoring/src/soh/train_gnn.py`:
```python
"""Training script for FleetGNN model.

Uses synthetic fleet scenarios for training:
- One degraded battery among healthy ones (outlier detection)
- Gradual fleet-wide degradation (calendar aging)
- Imbalanced fleet (mixed chemistry/age/capacity)
"""

from __future__ import annotations

import argparse
import logging
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

from soh.config import settings
from soh.gnn import FleetGNN, build_fleet_graph
from soh.synthetic import DegradationMode, generate_profile

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)-8s %(message)s")
logger = logging.getLogger("train_gnn")


def generate_fleet_scenario(
    n_batteries: int,
    rng: np.random.Generator,
    tsmixer_encoder: nn.Module | None = None,
) -> dict:
    """Generate a single fleet training scenario.

    Returns dict with keys: node_feats, voltages, currents, fleet_health, outlier_idx, imbalance
    """
    scenario_type = rng.choice(["healthy", "one_degraded", "fleet_degraded", "imbalanced"],
                                p=[0.25, 0.35, 0.20, 0.20])

    profiles = []
    if scenario_type == "healthy":
        for _ in range(n_batteries):
            profiles.append(generate_profile(DegradationMode.HEALTHY, rng=rng))
        fleet_health = rng.uniform(0.85, 1.0)
        outlier_idx = 0  # no real outlier
        imbalance = rng.uniform(0.0, 0.1)

    elif scenario_type == "one_degraded":
        outlier_idx = rng.integers(0, n_batteries)
        for i in range(n_batteries):
            if i == outlier_idx:
                mode = rng.choice([DegradationMode.CYCLE, DegradationMode.SUDDEN_FAILURE])
                profiles.append(generate_profile(mode, rng=rng, severity=rng.uniform(0.5, 1.0)))
            else:
                profiles.append(generate_profile(DegradationMode.HEALTHY, rng=rng))
        fleet_health = rng.uniform(0.5, 0.85)
        imbalance = rng.uniform(0.3, 0.8)

    elif scenario_type == "fleet_degraded":
        severity = rng.uniform(0.3, 0.8)
        for _ in range(n_batteries):
            profiles.append(generate_profile(DegradationMode.CALENDAR, rng=rng, severity=severity))
        fleet_health = max(0.0, 1.0 - severity * 0.5)
        outlier_idx = 0
        imbalance = rng.uniform(0.0, 0.2)

    else:  # imbalanced
        for i in range(n_batteries):
            severity = rng.uniform(0.0, 0.8)
            mode = rng.choice(list(DegradationMode))
            profiles.append(generate_profile(mode, rng=rng, severity=severity))
        # Outlier = most degraded
        soh_scores = [p.soh_score for p in profiles]
        outlier_idx = int(np.argmin(soh_scores))
        fleet_health = float(np.mean(soh_scores))
        imbalance = float(np.std(soh_scores))

    # Build node features: use TSMixer encoder if available, else use mean features
    if tsmixer_encoder is not None:
        with torch.no_grad():
            features_batch = torch.stack([torch.from_numpy(p.features) for p in profiles])
            node_feats = tsmixer_encoder(features_batch)
    else:
        # Fallback: mean of features per battery as node embedding (padded to 64)
        node_feats_list = []
        for p in profiles:
            mean_feats = p.features.mean(axis=0)  # (17,)
            padded = np.zeros(settings.gnn_node_features, dtype=np.float32)
            padded[:len(mean_feats)] = mean_feats
            node_feats_list.append(padded)
        node_feats = torch.from_numpy(np.array(node_feats_list))

    # Extract voltages and currents for edge features
    voltages = torch.tensor([p.features[:, 5].mean() for p in profiles])
    currents = torch.tensor([p.features[:, 10].mean() for p in profiles])

    return {
        "node_feats": node_feats,
        "voltages": voltages,
        "currents": currents,
        "fleet_health": fleet_health,
        "outlier_idx": outlier_idx,
        "outlier_score": 1.0 - profiles[outlier_idx].soh_score,
        "imbalance": min(1.0, imbalance),
    }


def train_gnn(
    output_dir: Path,
    n_scenarios: int = 2000,
    epochs: int = 30,
    lr: float = 1e-3,
    hidden: int = 32,
    n_layers: int = 2,
    heads: int = 4,
    seed: int = 42,
) -> dict:
    """Train FleetGNN on synthetic fleet scenarios."""
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    rng = np.random.default_rng(seed)

    model = FleetGNN(
        node_features=settings.gnn_node_features,
        hidden=hidden,
        n_layers=n_layers,
        heads=heads,
    ).to(device)

    n_params = sum(p.numel() for p in model.parameters())
    logger.info("FleetGNN: hidden=%d, layers=%d, heads=%d, params=%d", hidden, n_layers, heads, n_params)

    optimizer = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)

    # Pre-generate scenarios
    logger.info("Generating %d fleet scenarios...", n_scenarios)
    scenarios = []
    for _ in range(n_scenarios):
        n_bat = rng.integers(2, 24)
        scenarios.append(generate_fleet_scenario(n_bat, rng))

    split = int(0.8 * len(scenarios))
    train_scenarios = scenarios[:split]
    val_scenarios = scenarios[split:]

    criterion_health = nn.MSELoss()
    criterion_outlier = nn.MSELoss()
    criterion_imbalance = nn.MSELoss()

    best_val_loss = float("inf")
    best_epoch = -1
    best_state = None
    patience = 8
    patience_counter = 0
    t0 = time.time()

    for epoch in range(1, epochs + 1):
        # Train
        model.train()
        train_loss_sum = 0.0
        rng.shuffle(train_scenarios)

        for sc in train_scenarios:
            node_feats = sc["node_feats"].to(device)
            edge_index, edge_attr = build_fleet_graph(
                node_feats, sc["voltages"].to(device), sc["currents"].to(device)
            )

            fleet_health, _, outlier_score, imbalance = model(node_feats, edge_index, edge_attr)

            target_health = torch.tensor(sc["fleet_health"], device=device, dtype=torch.float)
            target_outlier = torch.tensor(sc["outlier_score"], device=device, dtype=torch.float)
            target_imbalance = torch.tensor(sc["imbalance"], device=device, dtype=torch.float)

            loss = (
                criterion_health(fleet_health, target_health)
                + 0.5 * criterion_outlier(outlier_score, target_outlier)
                + 0.3 * criterion_imbalance(imbalance, target_imbalance)
            )

            optimizer.zero_grad()
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            optimizer.step()

            train_loss_sum += loss.item()

        train_loss = train_loss_sum / len(train_scenarios)

        # Validate
        model.eval()
        val_loss_sum = 0.0
        with torch.no_grad():
            for sc in val_scenarios:
                node_feats = sc["node_feats"].to(device)
                edge_index, edge_attr = build_fleet_graph(
                    node_feats, sc["voltages"].to(device), sc["currents"].to(device)
                )
                fleet_health, _, outlier_score, imbalance = model(node_feats, edge_index, edge_attr)

                target_health = torch.tensor(sc["fleet_health"], device=device, dtype=torch.float)
                target_outlier = torch.tensor(sc["outlier_score"], device=device, dtype=torch.float)
                target_imbalance = torch.tensor(sc["imbalance"], device=device, dtype=torch.float)

                loss = (
                    criterion_health(fleet_health, target_health)
                    + 0.5 * criterion_outlier(outlier_score, target_outlier)
                    + 0.3 * criterion_imbalance(imbalance, target_imbalance)
                )
                val_loss_sum += loss.item()

        val_loss = val_loss_sum / len(val_scenarios)

        if epoch == 1 or epoch % 5 == 0 or epoch == epochs:
            logger.info("Epoch %3d/%d  train=%.6f  val=%.6f  (%.1fs)",
                        epoch, epochs, train_loss, val_loss, time.time() - t0)

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_epoch = epoch
            patience_counter = 0
            best_state = {k: v.cpu().clone() for k, v in model.state_dict().items()}
        else:
            patience_counter += 1
            if patience_counter >= patience:
                logger.info("Early stopping at epoch %d (best: %d)", epoch, best_epoch)
                break

    if best_state:
        model.load_state_dict(best_state)

    output_dir.mkdir(parents=True, exist_ok=True)
    pt_path = output_dir / "gnn_fleet.pt"
    torch.save({
        "model_state_dict": model.state_dict(),
        "node_features": settings.gnn_node_features,
        "hidden": hidden,
        "n_layers": n_layers,
        "heads": heads,
        "best_epoch": best_epoch,
        "best_val_loss": best_val_loss,
        "n_params": n_params,
    }, pt_path)

    logger.info("Saved FleetGNN: %s (%.1f KB)", pt_path, pt_path.stat().st_size / 1024)
    return {"n_params": n_params, "best_epoch": best_epoch, "best_val_loss": float(best_val_loss)}


def main():
    parser = argparse.ArgumentParser(description="Train FleetGNN for fleet-level scoring")
    parser.add_argument("--output-dir", type=Path, default=Path("models"))
    parser.add_argument("--n-scenarios", type=int, default=2000)
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--hidden", type=int, default=32)
    parser.add_argument("--n-layers", type=int, default=2)
    parser.add_argument("--heads", type=int, default=4)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    train_gnn(
        output_dir=args.output_dir,
        n_scenarios=args.n_scenarios,
        epochs=args.epochs,
        lr=args.lr,
        hidden=args.hidden,
        n_layers=args.n_layers,
        heads=args.heads,
        seed=args.seed,
    )


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run GNN tests**

```bash
cd services/soh-scoring && uv run pytest tests/test_gnn.py -v
```

**Commit:** `feat(soh-scoring): FleetGNN (GAT) for fleet health + outlier detection`

---

### Task 6: ONNX export + inference runner

**Files:**
- Create: `services/soh-scoring/src/soh/export_onnx.py`
- Create: `services/soh-scoring/src/soh/inference.py`
- Create: `services/soh-scoring/tests/test_inference.py`

- [ ] **Step 1: Write inference tests**

Create `services/soh-scoring/tests/test_inference.py`:
```python
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
```

- [ ] **Step 2: Implement ONNX export**

Create `services/soh-scoring/src/soh/export_onnx.py`:
```python
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
```

- [ ] **Step 3: Implement inference runner**

Create `services/soh-scoring/src/soh/inference.py`:
```python
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
```

- [ ] **Step 4: Run inference tests**

```bash
cd services/soh-scoring && uv run pytest tests/test_inference.py -v
```

**Commit:** `feat(soh-scoring): ONNX export + quantization + inference runner`

---

### Task 7: FastAPI REST API

**Files:**
- Create: `services/soh-scoring/src/soh/api.py`
- Create: `services/soh-scoring/tests/test_api.py`

- [ ] **Step 1: Write API integration tests**

Create `services/soh-scoring/tests/test_api.py`:
```python
"""Integration tests for SOH REST API."""

import pytest
from httpx import AsyncClient, ASGITransport

from soh.api import app


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
async def client():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as c:
        yield c


class TestHealthEndpoint:
    async def test_health(self, client):
        resp = await client.get("/health")
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"


class TestBatteriesEndpoint:
    async def test_get_batteries(self, client):
        resp = await client.get("/api/soh/batteries")
        assert resp.status_code == 200
        data = resp.json()
        assert isinstance(data, list)

    async def test_get_battery_by_id(self, client):
        resp = await client.get("/api/soh/battery/0")
        # May return 404 if no data, or 200 with data
        assert resp.status_code in (200, 404)


class TestFleetEndpoint:
    async def test_get_fleet(self, client):
        resp = await client.get("/api/soh/fleet")
        assert resp.status_code in (200, 404)


class TestPredictEndpoint:
    async def test_predict(self, client):
        resp = await client.post("/api/soh/predict")
        # Should trigger inference (may return 200 or 503 if models not loaded)
        assert resp.status_code in (200, 503)


class TestHistoryEndpoint:
    async def test_history(self, client):
        resp = await client.get("/api/soh/history/0?days=7")
        assert resp.status_code in (200, 404)
```

- [ ] **Step 2: Implement FastAPI application**

Create `services/soh-scoring/src/soh/api.py`:
```python
"""FastAPI REST API for SOH scoring results.

Endpoints:
    GET  /api/soh/batteries          → all battery scores (latest)
    GET  /api/soh/battery/{id}       → single battery score + history
    GET  /api/soh/fleet              → fleet score + outlier info
    POST /api/soh/predict            → force refresh (on-demand inference)
    GET  /api/soh/history/{id}       → SOH/RUL/anomaly time series
    GET  /health                     → health check
"""

from __future__ import annotations

import logging
import time
from contextlib import asynccontextmanager
from datetime import datetime, timezone

import uvicorn
from fastapi import FastAPI, HTTPException, Query
from pydantic import BaseModel

from soh.config import settings

logger = logging.getLogger(__name__)

# In-memory cache for latest scores (populated by scheduler)
_battery_cache: dict[int, dict] = {}
_fleet_cache: dict | None = None
_last_update: float = 0.0


class BatteryScore(BaseModel):
    battery: int
    soh_score: float
    rul_days: float
    anomaly_score: float
    r_int_trend_mohm_per_day: float | None = None
    timestamp: int


class FleetScore(BaseModel):
    fleet_health: float
    outlier_idx: int
    outlier_score: float
    imbalance_severity: float
    n_batteries: int
    timestamp: int


class HealthResponse(BaseModel):
    status: str
    last_update: str | None
    n_batteries: int
    models_loaded: bool


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Startup: try to load models. Shutdown: cleanup."""
    logger.info("SOH API starting on %s:%d", settings.api_host, settings.api_port)
    yield
    logger.info("SOH API shutting down")


app = FastAPI(
    title="KXKM BMU SOH Scoring API",
    version="0.1.0",
    lifespan=lifespan,
)


def update_cache(battery_scores: dict[int, dict], fleet_score: dict | None = None):
    """Update the in-memory score cache (called by scheduler)."""
    global _battery_cache, _fleet_cache, _last_update
    _battery_cache = battery_scores
    _fleet_cache = fleet_score
    _last_update = time.time()


@app.get("/health", response_model=HealthResponse)
async def health():
    return HealthResponse(
        status="ok",
        last_update=datetime.fromtimestamp(_last_update, tz=timezone.utc).isoformat() if _last_update > 0 else None,
        n_batteries=len(_battery_cache),
        models_loaded=len(_battery_cache) > 0,
    )


@app.get("/api/soh/batteries", response_model=list[BatteryScore])
async def get_all_batteries():
    if not _battery_cache:
        return []
    ts = int(_last_update)
    return [
        BatteryScore(battery=bid, timestamp=ts, **score)
        for bid, score in sorted(_battery_cache.items())
    ]


@app.get("/api/soh/battery/{battery_id}", response_model=BatteryScore)
async def get_battery(battery_id: int):
    if battery_id not in _battery_cache:
        raise HTTPException(status_code=404, detail=f"Battery {battery_id} not found")
    ts = int(_last_update)
    return BatteryScore(battery=battery_id, timestamp=ts, **_battery_cache[battery_id])


@app.get("/api/soh/fleet", response_model=FleetScore)
async def get_fleet():
    if not _fleet_cache:
        raise HTTPException(status_code=404, detail="No fleet score available")
    return FleetScore(
        n_batteries=len(_battery_cache),
        timestamp=int(_last_update),
        **_fleet_cache,
    )


@app.post("/api/soh/predict")
async def predict():
    """Trigger on-demand inference refresh."""
    try:
        from soh.scheduler import run_scoring_cycle
        results = run_scoring_cycle()
        return {"status": "ok", "n_batteries": len(results.get("battery_scores", {}))}
    except Exception as exc:
        logger.exception("On-demand prediction failed")
        raise HTTPException(status_code=503, detail=str(exc))


@app.get("/api/soh/history/{battery_id}")
async def get_history(battery_id: int, days: int = Query(default=30, ge=1, le=365)):
    """Query historical SOH/RUL/anomaly scores from InfluxDB."""
    try:
        from influxdb_client import InfluxDBClient

        client = InfluxDBClient(
            url=settings.influxdb_url,
            token=settings.influxdb_token,
            org=settings.influxdb_org,
        )
        query_api = client.query_api()

        flux = f'''
        from(bucket: "{settings.influxdb_output_bucket}")
          |> range(start: -{days}d)
          |> filter(fn: (r) => r._measurement == "soh_ml")
          |> filter(fn: (r) => r.battery == "{battery_id}")
          |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
          |> sort(columns: ["_time"])
        '''

        tables = query_api.query(flux)
        history = []
        for table in tables:
            for record in table.records:
                history.append({
                    "timestamp": int(record.get_time().timestamp()),
                    "soh_score": record.values.get("soh_score"),
                    "rul_days": record.values.get("rul_days"),
                    "anomaly_score": record.values.get("anomaly_score"),
                })
        client.close()

        if not history:
            raise HTTPException(status_code=404, detail=f"No history for battery {battery_id}")

        return {"battery": battery_id, "days": days, "history": history}

    except HTTPException:
        raise
    except Exception as exc:
        logger.exception("History query failed")
        raise HTTPException(status_code=503, detail=str(exc))


def main():
    uvicorn.run(
        "soh.api:app",
        host=settings.api_host,
        port=settings.api_port,
        log_level="info",
    )


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Run API tests**

```bash
cd services/soh-scoring && uv run pytest tests/test_api.py -v
```

**Commit:** `feat(soh-scoring): FastAPI REST API with 5 endpoints + health check`

---

### Task 8: Cron scheduling — ETL + inference + InfluxDB write

**Files:**
- Create: `services/soh-scoring/src/soh/scheduler.py`

- [ ] **Step 1: Implement scheduler**

Create `services/soh-scoring/src/soh/scheduler.py`:
```python
"""Cron scheduler: runs ETL + inference every 30 minutes, writes results to InfluxDB.

Pipeline:
1. ETL: query InfluxDB → compute feature windows per battery
2. TSMixer: score each battery (SOH, RUL, anomaly)
3. GNN: score fleet-level health
4. Write results to InfluxDB (soh_ml, soh_fleet measurements)
5. Update API cache
"""

from __future__ import annotations

import logging
import time
from datetime import datetime, timezone

from apscheduler.schedulers.blocking import BlockingScheduler
from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

from soh.api import update_cache
from soh.config import settings
from soh.etl import extract_all_batteries
from soh.inference import InferenceRunner

logger = logging.getLogger(__name__)

_runner: InferenceRunner | None = None


def get_runner() -> InferenceRunner:
    """Lazy-load inference runner."""
    global _runner
    if _runner is None:
        _runner = InferenceRunner()
    return _runner


def write_scores_to_influxdb(
    battery_scores: dict[int, dict],
    fleet_score: dict | None,
) -> None:
    """Write scoring results to InfluxDB."""
    client = InfluxDBClient(
        url=settings.influxdb_url,
        token=settings.influxdb_token,
        org=settings.influxdb_org,
    )
    write_api = client.write_api(write_options=SYNCHRONOUS)

    now = datetime.now(timezone.utc)

    # Per-battery scores
    points = []
    for bat_id, score in battery_scores.items():
        p = (
            Point("soh_ml")
            .tag("battery", str(bat_id))
            .field("soh_score", score["soh_score"])
            .field("rul_days", score["rul_days"])
            .field("anomaly_score", score["anomaly_score"])
            .time(now, WritePrecision.S)
        )
        points.append(p)

    # Fleet score
    if fleet_score:
        p = (
            Point("soh_fleet")
            .field("fleet_health", fleet_score["fleet_health"])
            .field("outlier_idx", fleet_score["outlier_idx"])
            .field("outlier_score", fleet_score["outlier_score"])
            .field("imbalance", fleet_score["imbalance_severity"])
            .time(now, WritePrecision.S)
        )
        points.append(p)

    write_api.write(bucket=settings.influxdb_output_bucket, record=points)
    client.close()
    logger.info("Wrote %d points to InfluxDB", len(points))


def run_scoring_cycle() -> dict:
    """Run one full scoring cycle: ETL → TSMixer → GNN → write."""
    t0 = time.time()
    logger.info("Starting scoring cycle...")

    # Step 1: ETL
    feature_windows = extract_all_batteries()
    if not feature_windows:
        logger.warning("No batteries with sufficient data, skipping cycle")
        return {"battery_scores": {}, "fleet_score": None}

    # Convert FeatureWindow objects to numpy arrays for inference
    features = {bid: fw.matrix for bid, fw in feature_windows.items()}

    # Step 2: TSMixer per-battery scoring
    runner = get_runner()
    battery_scores = runner.score_batteries(features)
    logger.info("TSMixer scored %d batteries", len(battery_scores))

    # Step 3: GNN fleet scoring
    fleet_score = runner.score_fleet(battery_scores, features)
    if fleet_score:
        logger.info("Fleet health: %.2f, outlier: bat %d (score %.2f)",
                     fleet_score["fleet_health"],
                     fleet_score["outlier_idx"],
                     fleet_score["outlier_score"])

    # Step 4: Write to InfluxDB
    try:
        write_scores_to_influxdb(battery_scores, fleet_score)
    except Exception:
        logger.exception("Failed to write scores to InfluxDB")

    # Step 5: Update API cache
    update_cache(battery_scores, fleet_score)

    elapsed = time.time() - t0
    logger.info("Scoring cycle complete in %.1fs (%d batteries)", elapsed, len(battery_scores))

    return {"battery_scores": battery_scores, "fleet_score": fleet_score}


def main():
    """Run scheduler with APScheduler."""
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)-8s %(message)s")
    logger.info("SOH Scheduler starting (interval: %d min)", settings.scoring_interval_min)

    # Run immediately on startup
    run_scoring_cycle()

    # Schedule periodic runs
    scheduler = BlockingScheduler()
    scheduler.add_job(
        run_scoring_cycle,
        "interval",
        minutes=settings.scoring_interval_min,
        id="soh_scoring",
        max_instances=1,
    )

    try:
        scheduler.start()
    except (KeyboardInterrupt, SystemExit):
        logger.info("Scheduler stopped")


if __name__ == "__main__":
    main()
```

**Commit:** `feat(soh-scoring): 30-min cron scheduler — ETL + inference + InfluxDB write`

---

### Task 9: Docker Compose

**Files:**
- Create: `services/soh-scoring/Dockerfile`
- Create: `services/soh-scoring/docker-compose.yml`

- [ ] **Step 1: Create Dockerfile**

Create `services/soh-scoring/Dockerfile`:
```dockerfile
# Multi-stage build for SOH scoring pipeline
FROM python:3.12-slim AS base

WORKDIR /app

# Install system dependencies for PyTorch + PyG
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    && rm -rf /var/lib/apt/lists/*

COPY pyproject.toml .
COPY src/ src/

RUN pip install --no-cache-dir -e .

# --- API service ---
FROM base AS soh-api
EXPOSE 8400
CMD ["soh-api"]

# --- Scheduler (ETL + scoring) ---
FROM base AS soh-scheduler
CMD ["soh-scheduler"]

# --- Training (includes dev deps) ---
FROM base AS soh-train
RUN pip install --no-cache-dir -e ".[dev]"
CMD ["bash"]
```

- [ ] **Step 2: Create docker-compose.yml**

Create `services/soh-scoring/docker-compose.yml`:
```yaml
# SOH Scoring Pipeline — Docker Compose for kxkm-ai
# Deploy: docker compose -f services/soh-scoring/docker-compose.yml up -d

version: "3.8"

services:
  soh-api:
    build:
      context: .
      target: soh-api
    container_name: soh-api
    restart: unless-stopped
    ports:
      - "8400:8400"
    environment:
      SOH_INFLUXDB_URL: http://influxdb:8086
      SOH_INFLUXDB_TOKEN: ${INFLUXDB_TOKEN}
      SOH_INFLUXDB_ORG: kxkm
      SOH_INFLUXDB_BUCKET: bmu
      SOH_INFLUXDB_OUTPUT_BUCKET: bmu
      SOH_TSMIXER_ONNX_PATH: /models/tsmixer_soh.onnx
      SOH_GNN_ONNX_PATH: /models/gnn_fleet.pt
    volumes:
      - model-data:/models
    networks:
      - kxkm
    depends_on:
      - soh-scheduler

  soh-scheduler:
    build:
      context: .
      target: soh-scheduler
    container_name: soh-scheduler
    restart: unless-stopped
    environment:
      SOH_INFLUXDB_URL: http://influxdb:8086
      SOH_INFLUXDB_TOKEN: ${INFLUXDB_TOKEN}
      SOH_INFLUXDB_ORG: kxkm
      SOH_INFLUXDB_BUCKET: bmu
      SOH_INFLUXDB_OUTPUT_BUCKET: bmu
      SOH_SCORING_INTERVAL_MIN: 30
      SOH_TSMIXER_ONNX_PATH: /models/tsmixer_soh.onnx
      SOH_GNN_ONNX_PATH: /models/gnn_fleet.pt
    volumes:
      - model-data:/models
    networks:
      - kxkm

  soh-train:
    build:
      context: .
      target: soh-train
    container_name: soh-train
    profiles: ["train"]  # Only starts with: docker compose --profile train up soh-train
    environment:
      SOH_INFLUXDB_URL: http://influxdb:8086
      SOH_INFLUXDB_TOKEN: ${INFLUXDB_TOKEN}
    volumes:
      - model-data:/models
      - ./src:/app/src  # Live code mount for development
    networks:
      - kxkm
    deploy:
      resources:
        reservations:
          devices:
            - capabilities: [gpu]

volumes:
  model-data:
    driver: local

networks:
  kxkm:
    external: true  # Shared with existing infra (MQTT, InfluxDB, Grafana)
```

- [ ] **Step 3: Verify Docker build**

```bash
cd services/soh-scoring && docker build --target soh-api -t kxkm-soh-api:test .
```

**Commit:** `feat(soh-scoring): Dockerfile + docker-compose for 3 services (api, scheduler, train)`

---

### Task 10: Tests — full suite verification

**Files:**
- All test files from previous tasks

- [ ] **Step 1: Run full test suite**

```bash
cd services/soh-scoring && uv run pytest tests/ -v --tb=short
```

- [ ] **Step 2: Run type check (ruff)**

```bash
cd services/soh-scoring && uv run ruff check src/ tests/
```

- [ ] **Step 3: Verify end-to-end flow with synthetic data**

```bash
cd services/soh-scoring && uv run python -c "
from soh.synthetic import generate_dataset
from soh.tsmixer import TSMixer
from soh.gnn import FleetGNN, build_fleet_graph
import torch
import numpy as np

# Generate data
X, y_soh, y_rul, y_anomaly = generate_dataset(n_samples=10, seq_len=336)
print(f'Synthetic: X={X.shape}')

# TSMixer forward pass
model = TSMixer(n_features=17, hidden=64, n_mix_layers=3)
x = torch.from_numpy(X)
soh, rul, anomaly = model(x)
print(f'TSMixer: soh={soh.shape}, rul={rul.shape}, anomaly={anomaly.shape}')

# TSMixer hidden state for GNN
hidden = model.encode(x)
print(f'TSMixer encode: hidden={hidden.shape}')

# GNN forward pass
gnn = FleetGNN(node_features=64, hidden=32)
voltages = torch.from_numpy(X[:, :, 5].mean(axis=1))
currents = torch.from_numpy(X[:, :, 10].mean(axis=1))
edge_index, edge_attr = build_fleet_graph(hidden, voltages, currents)
fleet_health, outlier_idx, outlier_score, imbalance = gnn(hidden, edge_index, edge_attr)
print(f'GNN: fleet_health={fleet_health:.3f}, outlier={outlier_idx}, score={outlier_score:.3f}, imbalance={imbalance:.3f}')
print('E2E OK')
"
```

**Commit:** `test(soh-scoring): full test suite — features, models, inference, API`

---

## Deployment Playbook (kxkm-ai)

After all tasks pass locally:

```bash
# 1. Push code to kxkm-ai
git archive HEAD -- services/soh-scoring | ssh kxkm@kxkm-ai "mkdir -p /home/kxkm/soh-scoring && tar -C /home/kxkm/soh-scoring -xf -"

# 2. Train models on GPU
ssh kxkm@kxkm-ai "cd /home/kxkm/soh-scoring/services/soh-scoring && \
    docker compose --profile train run soh-train bash -c '\
        soh-train-tsmixer --output-dir /models --n-synthetic 10000 --epochs 50 && \
        soh-train-gnn --output-dir /models --n-scenarios 5000 --epochs 30 && \
        soh-export --tsmixer /models/tsmixer_soh.pt --gnn /models/gnn_fleet.pt --output-dir /models \
    '"

# 3. Start services
ssh kxkm@kxkm-ai "cd /home/kxkm/soh-scoring/services/soh-scoring && \
    docker compose up -d soh-scheduler soh-api"

# 4. Verify
curl http://kxkm-ai:8400/health
curl http://kxkm-ai:8400/api/soh/batteries
curl http://kxkm-ai:8400/api/soh/fleet
```

---

## Quality Gates

| Gate | Criterion | Check |
|------|-----------|-------|
| Unit tests | All pass | `uv run pytest tests/ -v` |
| Lint | No errors | `uv run ruff check src/ tests/` |
| TSMixer params | < 600K | Model summary in training log |
| GNN params | < 250K | Model summary in training log |
| TSMixer SOH MAPE | < 15% (synthetic) | Training metrics |
| API health | 200 OK | `curl /health` |
| Docker build | All 3 targets build | `docker build --target <each>` |
| E2E inference | Scores all batteries | POST /api/soh/predict returns 200 |
