#!/usr/bin/env python3
"""Adapt extracted features to train_fpnn.py input schema.

Reads features.parquet (output of extract_features.py) and adds/renames columns
to match train_fpnn.py FEATURE_COLS specification.
"""

import argparse
import logging
import sys
from pathlib import Path

import pandas as pd
import numpy as np

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s %(message)s",
)
log = logging.getLogger(__name__)


def adapt_features(input_path: str, output_path: str) -> None:
    """Adapt extracted features to train_fpnn schema."""
    
    input_file = Path(input_path)
    output_file = Path(output_path)
    
    # Read extracted features
    log.info("Reading %s", input_file)
    df = pd.read_parquet(input_file)
    
    log.info("Input shape: %s", df.shape)
    log.info("Input columns: %s", list(df.columns))
    
    # Rename columns to match train_fpnn expectations (lowercase)
    df = df.rename(columns={
        "Ah_discharge": "ah_cons",
        "Ah_charge": "ah_charge",
        "n_samples": "samples",
    })
    
    # Add missing columns derived from existing ones
    # V_min, V_max: use V_mean ± V_std as proxy
    df["V_min"] = df["V_mean"] - df["V_std"]
    df["V_max"] = df["V_mean"] + df["V_std"]
    
    # I_max: use absolute maximum of I_mean + I_std
    df["I_max"] = (df["I_mean"].abs() + df["I_std"]).abs()
    
    # Ensure all required columns exist
    required_cols = [
        "V_mean", "V_std", "I_mean", "I_std", "dV_dt", "dI_dt",
        "ah_cons", "ah_charge", "V_min", "V_max", "I_max", "samples",
        "R_internal", "device", "channel"
    ]
    
    missing = [c for c in required_cols if c not in df.columns]
    if missing:
        log.error("Missing columns after adaptation: %s", missing)
        sys.exit(1)
    
    log.info("After adaptation:")
    log.info("  V_min: min=%.4f, max=%.4f, mean=%.4f", 
             df["V_min"].min(), df["V_min"].max(), df["V_min"].mean())
    log.info("  V_max: min=%.4f, max=%.4f, mean=%.4f", 
             df["V_max"].min(), df["V_max"].max(), df["V_max"].mean())
    log.info("  I_max: min=%.4f, max=%.4f, mean=%.4f", 
             df["I_max"].min(), df["I_max"].max(), df["I_max"].mean())
    
    # Save adapted features
    df.to_parquet(output_file)
    log.info("Saved adapted features: %s (%s)", output_file, df.shape)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Adapt extracted features to train_fpnn input schema."
    )
    parser.add_argument("--input", required=True, help="Path to features.parquet")
    parser.add_argument("--output", required=True, help="Path to adapted output")
    args = parser.parse_args()
    
    adapt_features(args.input, args.output)
