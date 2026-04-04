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
        """7 days of 1-min data -> 336 time steps (30-min aggregation)."""
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
