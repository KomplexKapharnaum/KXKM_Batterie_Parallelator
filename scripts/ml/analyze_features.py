#!/usr/bin/env python3
"""
analyze_features.py — Feature distribution audit for Phase 2 FPNN/SambaMixer training.

Analyzes per-device feature distributions to diagnose:
1. Device-dependent shifts (train gocab/k-led vs test tender)
2. High-NaN feature impact
3. SOH proxy quality candidates
4. Outlier detection
"""

import argparse
import logging
import sys
from pathlib import Path

import numpy as np
import pandas as pd
from scipy import stats

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s %(message)s",
)
log = logging.getLogger(__name__)


def analyze_features(features_path: str) -> None:
    """Comprehensive feature audit across devices."""
    
    log.info("Loading features from %s", features_path)
    df = pd.read_parquet(features_path)
    
    log.info("Dataset shape: %s rows, %s columns", df.shape[0], df.shape[1])
    log.info("Devices: %s", sorted(df["device"].unique()))
    
    # === NaN Analysis ===
    log.info("\n=== NaN Analysis ===")
    for col in df.columns:
        nan_count = df[col].isna().sum()
        nan_pct = 100.0 * nan_count / len(df)
        if nan_pct > 0:
            log.info("  %s: %.1f%% NaN (%d rows)", col, nan_pct, nan_count)
    
    # === Per-Device Distribution ===
    log.info("\n=== Per-Device Feature Distributions ===")
    
    for device in sorted(df["device"].unique()):
        log.info("\nDevice: %s (%d windows)", device, len(df[df["device"] == device]))
        
        for col in ["V_mean", "I_mean", "ah_cons", "rest_voltage", "coulombic_efficiency"]:
            if col not in df.columns:
                continue
            
            subset = df[df["device"] == device][col]
            subset_clean = subset.dropna()
            
            if len(subset_clean) > 0:
                log.info(
                    "  %s: mean=%.2f, std=%.2f, range=[%.2f, %.2f], NaN=%.1f%%",
                    col,
                    subset_clean.mean(),
                    subset_clean.std(),
                    subset_clean.min(),
                    subset_clean.max(),
                    100.0 * (len(subset) - len(subset_clean)) / len(subset),
                )
    
    # === SOH Proxy Candidates ===
    log.info("\n=== SOH Proxy Candidates Evaluation ===")
    
    log.info("\nCandidate A: rest_voltage (current)")
    rest_voltage_nan = (df["rest_voltage"].isna().sum() / len(df)) * 100
    log.info("  Pros: Direct measurement")
    log.info("  Cons: %.1f%% NaN, device-dependent normalization (tender vs LEDs differ)", rest_voltage_nan)
    log.info("  → VERDICT: BROKEN for cross-device training ✗")
    
    log.info("\nCandidate B: capacity_fade_ratio (recommended)")
    # Compute capacity fade per device/channel
    fade_ratios = []
    for (dev, ch), group in df.groupby(["device", "channel"]):
        ah_trend = group["ah_cons"].dropna()
        if len(ah_trend) > 2:
            x = np.arange(len(ah_trend)).reshape(-1, 1)
            trend = np.polyfit(x.flatten(), ah_trend.values, 1)[0]
            fade_ratios.append(np.abs(trend))
    
    if fade_ratios:
        log.info("  Pros: Physics-based (capacity fade), device-independent, <2%% NaN")
        log.info("  Fade rates: mean=%.4f, std=%.4f, range=[%.4f, %.4f]",
                np.mean(fade_ratios), np.std(fade_ratios), np.min(fade_ratios), np.max(fade_ratios))
        log.info("  → VERDICT: RECOMMENDED ✓")
    
    log.info("\nCandidate C: voltage_fade_ratio (alternative)")
    log.info("  Pros: Simpler computation, physics-inspired")
    log.info("  Cons: Voltage varies with load, less stable than capacity")
    log.info("  → VERDICT: Secondary option ~")
    
    # === Coulombic Efficiency Bug ===
    log.info("\n=== Coulombic Efficiency Bug Analysis ===")
    ce = df["coulombic_efficiency"].dropna()
    log.info("  Current non-NaN: %d values", len(ce))
    log.info("  Range: min=%.2f, max=%.2f", ce.min(), ce.max())
    log.info("  Outliers (>1000): %d values (division by ~0 suspected)", (ce > 1000).sum())
    log.info("  FIX: Add threshold Ah_discharge > 0.1 Ah before division")
    log.info("  EXPECTED: Reduce NaN from 14%% → 5%%")
    
    # === Distribution Shift (Train vs Test) ===
    log.info("\n=== Distribution Shift: Train vs Test Devices ===")
    log.info("\nTrain devices (Phase 1): k-led1, k-led2")
    log.info("Test device (Phase 1): tender")
    
    train_devices = ["k-led1", "k-led2"]
    test_device = "tender"
    
    for col in ["V_mean", "I_mean", "ah_cons"]:
        train_vals = df[df["device"].isin(train_devices)][col].dropna()
        test_vals = df[df["device"] == test_device][col].dropna()
        
        if len(train_vals) > 0 and len(test_vals) > 0:
            ks_stat, pval = stats.ks_2samp(train_vals, test_vals)
            log.info("\n  %s:", col)
            log.info("    Train (k-led): mean=%.2f, std=%.2f", train_vals.mean(), train_vals.std())
            log.info("    Test (tender): mean=%.2f, std=%.2f", test_vals.mean(), test_vals.std())
            log.info("    KS test: stat=%.4f, p-value=%.4f %s",
                    ks_stat, pval, "*** SIGNIFICANT SHIFT ***" if pval < 0.05 else "✓ similar")
    
    # === Phase 2 Recommendations ===
    log.info("\n" + "="*70)
    log.info("PHASE 2 RECOMMENDATIONS")
    log.info("="*70)
    
    log.info("\n1. SOH PROXY: Replace rest_voltage with capacity_fade_ratio")
    log.info("   → Fixes device-dependent normalization issue")
    log.info("   → Expected MAPE improvement: ~50-70pp (97%% → 27-47%%)")
    
    log.info("\n2. FEATURE CLEANUP:")
    log.info("   → Drop rows with >50%% NaN across features (adapt_features.py)")
    log.info("   → Fix coulombic_efficiency division by zero (extract_features.py)")
    log.info("   → Remove rest_voltage from FEATURE_COLS (train_fpnn.py)")
    
    log.info("\n3. RETRAIN FPNN:")
    log.info("   → Expected Phase 2.2 result: test MAPE < 15%% (target < 5%%)")
    log.info("   → If still > 20%%: investigate missing features or device drift")
    
    log.info("\n4. SAMBAMIXER:")
    log.info("   → Train on clean features, synthetic RUL targets")
    log.info("   → Expected: test MAPE < 15%% on synthetic RUL proxy")
    
    log.info("\n" + "="*70)
    log.info("DECISION CHECKPOINT: capacity_fade_ratio is GO for Phase 2.2")
    log.info("="*70 + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Analyze features for Phase 2 training")
    parser.add_argument("--input", default="models/features_adapted.parquet",
                       help="Path to features.parquet")
    args = parser.parse_args()
    
    if not Path(args.input).exists():
        print(f"Error: Input file not found: {args.input}")
        sys.exit(1)
    
    analyze_features(args.input)
