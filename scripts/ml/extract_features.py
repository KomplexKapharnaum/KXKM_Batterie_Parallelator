#!/usr/bin/env python3
"""Extract battery health features from consolidated Parallelator data.

Reads consolidated.parquet (output of parse_csv.py) and computes per-channel
features over sliding time windows for ML model training.

Features computed per (device, channel, window):
  - V_mean, V_std: voltage statistics
  - I_mean, I_std: current statistics
  - dV_dt, dI_dt: voltage/current rate of change (finite difference)
  - R_internal: estimated internal resistance at load transitions
  - Ah_discharge, Ah_charge: cumulative amp-hours
  - coulombic_efficiency: Ah_charge / Ah_discharge
  - rest_voltage: mean voltage when current ~ 0 for > 30s
  - cycle_count: number of current zero-crossings (charge/discharge transitions)
"""

import argparse
import logging
import sys
from pathlib import Path

import numpy as np
import pandas as pd

logger = logging.getLogger(__name__)

# Threshold below which current is considered "zero" (amps)
CURRENT_ZERO_THRESHOLD = 0.05

# Minimum rest duration (seconds) to qualify as rest period
REST_MIN_DURATION_S = 30.0

# Minimum current delta for internal resistance calculation
MIN_DELTA_I = 0.1


def compute_window_features(group: pd.DataFrame, window_seconds: float) -> list[dict]:
    """Compute features for all sliding windows within a (device, channel) group.

    Parameters
    ----------
    group : pd.DataFrame
        Sorted by timestamp, single device+channel.
    window_seconds : float
        Window duration in seconds.

    Returns
    -------
    list[dict]
        One dict per window with all computed features.
    """
    if group.empty or len(group) < 2:
        return []

    device = group["device"].iloc[0]
    channel = group["channel"].iloc[0]

    ts = group["timestamp"].values.astype("datetime64[ns]")
    voltage = group["voltage"].values.astype(np.float64)
    current = group["current"].values.astype(np.float64)
    ah_cons = group["ah_cons"].values.astype(np.float64)
    ah_charge = group["ah_charge"].values.astype(np.float64)

    # Elapsed seconds from start
    t0 = ts[0]
    elapsed_s = (ts - t0).astype(np.float64) / 1e9  # ns -> s
    total_duration = elapsed_s[-1]

    if total_duration < window_seconds:
        # Single window covering all data
        window_starts = [0.0]
    else:
        # Non-overlapping windows
        window_starts = np.arange(0, total_duration, window_seconds).tolist()

    results = []

    for w_start in window_starts:
        w_end = w_start + window_seconds
        mask = (elapsed_s >= w_start) & (elapsed_s < w_end)
        n_pts = mask.sum()

        if n_pts < 2:
            continue

        w_v = voltage[mask]
        w_i = current[mask]
        w_t = elapsed_s[mask]
        w_ah_cons = ah_cons[mask]
        w_ah_charge = ah_charge[mask]
        w_ts = ts[mask]

        dt = np.diff(w_t)
        dt_safe = np.where(dt > 0, dt, np.nan)

        # Basic statistics
        v_mean = np.nanmean(w_v)
        v_std = np.nanstd(w_v)
        i_mean = np.nanmean(w_i)
        i_std = np.nanstd(w_i)

        # Rate of change (mean of finite differences)
        dv = np.diff(w_v)
        di = np.diff(w_i)
        dv_dt = np.nanmean(dv / dt_safe) if len(dt_safe) > 0 else 0.0
        di_dt = np.nanmean(di / dt_safe) if len(dt_safe) > 0 else 0.0

        # Internal resistance estimation: R = dV / dI at load transitions
        # Look for points where current changes significantly
        r_internal = _estimate_r_internal(w_v, w_i)

        # Cumulative Ah
        ah_discharge = float(w_ah_cons[-1] - w_ah_cons[0])
        ah_charge_cum = float(w_ah_charge[-1] - w_ah_charge[0])

        # Coulombic efficiency
        if ah_discharge > 0:
            coulombic_eff = ah_charge_cum / ah_discharge
        else:
            coulombic_eff = np.nan

        # Rest voltage: mean V when |I| < threshold for > REST_MIN_DURATION_S
        rest_v = _compute_rest_voltage(w_v, w_i, w_t)

        # Cycle count: zero-crossings on current (sign changes above threshold)
        cycle_count = _count_cycles(w_i)

        results.append(
            {
                "device": device,
                "channel": channel,
                "window_start": pd.Timestamp(w_ts[0]),
                "window_end": pd.Timestamp(w_ts[-1]),
                "n_samples": int(n_pts),
                "V_mean": v_mean,
                "V_std": v_std,
                "I_mean": i_mean,
                "I_std": i_std,
                "dV_dt": dv_dt,
                "dI_dt": di_dt,
                "R_internal": r_internal,
                "Ah_discharge": ah_discharge,
                "Ah_charge": ah_charge_cum,
                "coulombic_efficiency": coulombic_eff,
                "rest_voltage": rest_v,
                "cycle_count": cycle_count,
            }
        )

    return results


def _estimate_r_internal(voltage: np.ndarray, current: np.ndarray) -> float:
    """Estimate internal resistance from load transitions.

    Finds points where current changes by > MIN_DELTA_I between consecutive
    samples and computes R = delta_V / delta_I. Returns median of estimates.
    """
    di = np.diff(current)
    dv = np.diff(voltage)

    # Find significant current transitions
    transition_mask = np.abs(di) > MIN_DELTA_I
    if not transition_mask.any():
        return np.nan

    r_estimates = np.abs(dv[transition_mask] / di[transition_mask])

    # Filter out extreme outliers (> 10 ohm is unrealistic for battery)
    r_estimates = r_estimates[(r_estimates > 0) & (r_estimates < 10.0)]

    if len(r_estimates) == 0:
        return np.nan

    return float(np.median(r_estimates))


def _compute_rest_voltage(
    voltage: np.ndarray, current: np.ndarray, elapsed_s: np.ndarray
) -> float:
    """Compute mean voltage during rest periods (|I| < threshold for > 30s)."""
    is_rest = np.abs(current) < CURRENT_ZERO_THRESHOLD

    if not is_rest.any():
        return np.nan

    # Find contiguous rest periods
    rest_voltages = []
    in_rest = False
    rest_start_idx = 0

    for i in range(len(is_rest)):
        if is_rest[i] and not in_rest:
            in_rest = True
            rest_start_idx = i
        elif not is_rest[i] and in_rest:
            in_rest = False
            duration = elapsed_s[i - 1] - elapsed_s[rest_start_idx]
            if duration >= REST_MIN_DURATION_S:
                rest_voltages.extend(voltage[rest_start_idx:i].tolist())

    # Handle rest period that extends to end of window
    if in_rest:
        duration = elapsed_s[-1] - elapsed_s[rest_start_idx]
        if duration >= REST_MIN_DURATION_S:
            rest_voltages.extend(voltage[rest_start_idx:].tolist())

    if not rest_voltages:
        return np.nan

    return float(np.mean(rest_voltages))


def _count_cycles(current: np.ndarray) -> int:
    """Count charge/discharge cycles via zero-crossings on current.

    A cycle boundary is where current crosses the zero threshold in either
    direction. Returns number of zero-crossings / 2 (each full cycle has
    two crossings).
    """
    above = current > CURRENT_ZERO_THRESHOLD
    below = current < -CURRENT_ZERO_THRESHOLD

    # Assign sign: +1 (discharging), -1 (charging), 0 (rest)
    sign = np.zeros_like(current, dtype=np.int8)
    sign[above] = 1
    sign[below] = -1

    # Remove zeros to find actual sign changes
    nonzero = sign[sign != 0]
    if len(nonzero) < 2:
        return 0

    crossings = np.sum(np.diff(nonzero) != 0)
    return int(crossings // 2)


def extract_features(input_path: Path, window_seconds: float) -> pd.DataFrame:
    """Read consolidated parquet and extract features for all device/channel pairs."""
    logger.info("Reading consolidated data from %s", input_path)
    df = pd.read_parquet(input_path, engine="pyarrow")
    logger.info("Loaded %d rows", len(df))

    # Ensure sorted
    df.sort_values(["device", "channel", "timestamp"], inplace=True)

    all_features = []
    groups = df.groupby(["device", "channel"])
    total_groups = len(groups)

    for idx, ((device, channel), group) in enumerate(groups, 1):
        logger.debug(
            "Processing device=%s channel=%d (%d/%d, %d rows)",
            device,
            channel,
            idx,
            total_groups,
            len(group),
        )
        features = compute_window_features(group, window_seconds)
        all_features.extend(features)

    if not all_features:
        logger.error("No features extracted from any group")
        sys.exit(1)

    features_df = pd.DataFrame(all_features)
    logger.info("Extracted %d feature windows", len(features_df))

    return features_df


def print_feature_stats(df: pd.DataFrame) -> None:
    """Log feature statistics."""
    logger.info("=== Feature Statistics ===")
    logger.info("Total windows: %d", len(df))
    logger.info("Devices: %s", sorted(df["device"].unique()))
    logger.info("Channels: %s", sorted(df["channel"].unique()))

    numeric_cols = [
        "V_mean",
        "V_std",
        "I_mean",
        "I_std",
        "dV_dt",
        "dI_dt",
        "R_internal",
        "Ah_discharge",
        "Ah_charge",
        "coulombic_efficiency",
        "rest_voltage",
        "cycle_count",
    ]

    for col in numeric_cols:
        if col not in df.columns:
            continue
        valid = df[col].dropna()
        if len(valid) == 0:
            logger.info("  %s: all NaN", col)
            continue
        logger.info(
            "  %s: count=%d, min=%.4f, mean=%.4f, max=%.4f, std=%.4f, nan=%d",
            col,
            len(valid),
            valid.min(),
            valid.mean(),
            valid.max(),
            valid.std(),
            df[col].isna().sum(),
        )

    # Per-device window counts
    logger.info("--- Per-device window counts ---")
    for device, count in df.groupby("device").size().items():
        logger.info("  %s: %d windows", device, count)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Extract battery health features from consolidated Parallelator data"
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("data/consolidated.parquet"),
        help="Input consolidated parquet file (default: data/consolidated.parquet)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/features.parquet"),
        help="Output features parquet file (default: data/features.parquet)",
    )
    parser.add_argument(
        "--window",
        type=float,
        default=60.0,
        help="Sliding window duration in seconds (default: 60)",
    )
    parser.add_argument(
        "--log-level",
        default="INFO",
        choices=["DEBUG", "INFO", "WARNING", "ERROR"],
        help="Logging level (default: INFO)",
    )
    args = parser.parse_args()

    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
        datefmt="%Y-%m-%d %H:%M:%S",
    )

    input_path = args.input.resolve()
    output_path = args.output.resolve()

    if not input_path.is_file():
        logger.error("Input file does not exist: %s", input_path)
        sys.exit(1)

    logger.info("Input: %s", input_path)
    logger.info("Output: %s", output_path)
    logger.info("Window: %.1f seconds", args.window)

    features_df = extract_features(input_path, args.window)
    print_feature_stats(features_df)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    features_df.to_parquet(output_path, engine="pyarrow", index=False)
    size_mb = output_path.stat().st_size / (1024 * 1024)
    logger.info("Saved features parquet: %s (%.2f MB)", output_path, size_mb)


if __name__ == "__main__":
    main()
