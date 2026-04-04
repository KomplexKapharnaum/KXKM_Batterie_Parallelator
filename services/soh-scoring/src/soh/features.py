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
