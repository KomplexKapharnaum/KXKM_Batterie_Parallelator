#!/usr/bin/env python3
"""
create_rul_labels.py — Generate synthetic RUL (Remaining Useful Life) labels for training.

RUL is estimated from observed capacity fade trends per device+channel.
For each device+channel, we fit a linear trend to Ah_discharge (cumulative discharge)
and extrapolate to end-of-life criterion.

EOL threshold: reduction to 80% nominal capacity (standard battery definition).
"""

import argparse
import logging
from pathlib import Path

import numpy as np
import pandas as pd
from scipy import stats

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s %(message)s",
)
log = logging.getLogger(__name__)

# End-of-life: when capacity fades to 80% of nominal
EOL_THRESHOLD = 0.80


def estimate_rul_per_channel(group: pd.DataFrame) -> pd.Series:
    """
    Estimate RUL per device+channel from capacity fade trend.
    
    group: DataFrame for single device+channel, sorted by window_start
    Returns: Series of RUL values (in units of windows)
    """
    device = group["device"].iloc[0]
    channel = group["channel"].iloc[0]
    
    # Get ah_cons time series
    ah_series = group["ah_cons"].dropna()
    
    if len(ah_series) < 3:
        # Not enough data to estimate trend
        return pd.Series([np.nan] * len(group), index=group.index)
    
    # Fit linear trend to ah_cons
    x = np.arange(len(ah_series)).reshape(-1, 1).flatten()
    y = ah_series.values
    
    # Linear regression: ah_cons = a + b*t
    slope, intercept, r_value, p_value, std_err = stats.linregress(x, y)
    
    # Nominal capacity: max observed ah_cons
    nom_capacity = np.max(y)
    
    # EOL point: when capacity fades to 80% of nominal
    eol_capacity = EOL_THRESHOLD * nom_capacity
    
    # Remaining cycles until EOL
    # If slope is positive (capacity increasing with time), RUL is infinite
    # If slope is negative (capacity fading), RUL = (current - EOL) / |slope|
    # If constant, no fade detected, RUL undefined
    
    ruls = []
    for idx in range(len(group)):
        current_ah = group["ah_cons"].iloc[idx]
        
        if pd.isna(current_ah):
            ruls.append(np.nan)
        elif slope < -0.001:  # Significant fade detected
            # Cycles remaining until EOL
            cycles_remaining = (current_ah - eol_capacity) / (-slope)
            ruls.append(max(0.0, cycles_remaining))  # Clamp at 0
        else:
            # No significant fade or positive trend
            ruls.append(np.nan)
    
    return pd.Series(ruls, index=group.index)


def create_rul_labels(input_path: str, output_path: str) -> None:
    """Generate RUL labels from features."""
    
    log.info("Loading features from %s", input_path)
    df = pd.read_parquet(input_path)
    
    log.info("Dataset shape: %s", df.shape)
    log.info("Devices: %s", sorted(df["device"].unique()))
    
    # Ensure sorted by device, channel, window_start
    df = df.sort_values(["device", "channel", "window_start"])
    
    # Generate RUL per device+channel
    log.info("Estimating RUL per device+channel...")
    ruls = []
    
    for (device, channel), group in df.groupby(["device", "channel"]):
        group_ruls = estimate_rul_per_channel(group)
        ruls.append(group_ruls)
    
    df["rul"] = pd.concat(ruls)
    
    # Statistics
    rul_clean = df["rul"].dropna()
    log.info("\nRUL Statistics:")
    log.info("  Total rows: %d", len(df))
    log.info("  RUL defined: %d (%.1f%%)", len(rul_clean), 100.0 * len(rul_clean) / len(df))
    log.info("  RUL range: [%.2f, %.2f] windows", rul_clean.min(), rul_clean.max())
    log.info("  RUL mean: %.2f, std: %.2f", rul_clean.mean(), rul_clean.std())
    
    # Per-device summary
    log.info("\nPer-device RUL summary:")
    for device in sorted(df["device"].unique()):
        device_rul = df[df["device"] == device]["rul"].dropna()
        if len(device_rul) > 0:
            log.info("  %s: %d samples, mean=%.2f, range=[%.2f, %.2f]",
                    device, len(device_rul), device_rul.mean(), device_rul.min(), device_rul.max())
    
    # Save
    log.info("Saving RUL labels to %s", output_path)
    df.to_parquet(output_path, engine="pyarrow", index=False)
    log.info("Done: %s", output_path)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Generate RUL labels from features")
    parser.add_argument("--input", required=True, help="Path to features.parquet")
    parser.add_argument("--output", required=True, help="Path to output parquet with RUL")
    args = parser.parse_args()
    
    create_rul_labels(args.input, args.output)
