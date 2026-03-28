#!/usr/bin/env python3
"""
create_rul_labels.py — Generate synthetic RUL (Remaining Useful Life) labels for training.

RUL is estimated from observed capacity fade per device+channel.
RUL = normalized capacity remaining = 100 * (1 - current_ah / max_ah)

This represents percentage of original capacity still available (0% = depleted, 100% = fresh).
"""

import argparse
import logging
from pathlib import Path

import numpy as np
import pandas as pd

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s %(message)s",
)
log = logging.getLogger(__name__)


def estimate_rul_per_channel(group: pd.DataFrame) -> pd.Series:
    """
    Estimate RUL per device+channel from normalized capacity remaining.
    
    group: DataFrame for single device+channel
    Returns: Series of RUL values (percentage remaining, 0-100)
    """
    ah_series = group["ah_cons"].dropna()
    
    if len(ah_series) == 0:
        return pd.Series([np.nan] * len(group), index=group.index)
    
    # Nominal (max) capacity observed in this device+channel
    nom_capacity = np.max(ah_series)
    
    if nom_capacity <= 0.001:  # Threshold to avoid div by zero
        return pd.Series([np.nan] * len(group), index=group.index)
    
    # RUL = percentage of capacity remaining (0% = dead, 100% = new)
    ruls = []
    for idx in range(len(group)):
        current_ah = group["ah_cons"].iloc[idx]
        
        if pd.isna(current_ah):
            ruls.append(np.nan)
        else:
            # Percentage of nominal capacity currently available
            remaining = 100.0 * (1.0 - current_ah / nom_capacity)
            ruls.append(max(0.0, remaining))  # Clamp at 0
    
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
    
    if len(rul_clean) > 0:
        log.info("  RUL range: [%.2f, %.2f] %% capacity remaining", rul_clean.min(), rul_clean.max())
        log.info("  RUL mean: %.2f, std: %.2f", rul_clean.mean(), rul_clean.std())
        
        # Per-device summary
        log.info("\nPer-device RUL summary:")
        for device in sorted(df["device"].unique()):
            device_rul = df[df["device"] == device]["rul"].dropna()
            if len(device_rul) > 0:
                log.info("  %s: %d samples, mean=%.2f%%, range=[%.2f, %.2f]",
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
