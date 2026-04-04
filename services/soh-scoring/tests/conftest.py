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
