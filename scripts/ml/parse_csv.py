#!/usr/bin/env python3
"""Parse KXKM Parallelator SD card CSV logs into a consolidated parquet file.

Reads all CSV files from the SD card log directory, unpivots per-channel
columns into a long-format DataFrame, and writes consolidated.parquet.

CSV format (semicolon-separated):
  Temps;Volt_1..8;Current_1..8;Switch_1..8;AhCons_1..8;AhCharge_1..8;TotCurrent;TotCharge;TotCons

Device name is extracted from the filename pattern: datalog_{device}_{seq}.csv
"""

import argparse
import logging
import re
import sys
from pathlib import Path

import pandas as pd

logger = logging.getLogger(__name__)

NUM_CHANNELS = 8

# Column groups that exist per channel (suffix _1 .. _8)
CHANNEL_GROUPS = {
    "voltage": "Volt",
    "current": "Current",
    "switch": "Switch",
    "ah_cons": "AhCons",
    "ah_charge": "AhCharge",
}

DEVICE_PATTERN = re.compile(r"datalog_(.+?)_\d{3}\.csv$")


def extract_device_name(filepath: Path) -> str:
    """Extract device name from filename like datalog_k-led1_003.csv -> k-led1."""
    match = DEVICE_PATTERN.search(filepath.name)
    if match:
        return match.group(1)
    # Fallback: strip extension and use full stem
    logger.warning("Could not extract device name from %s, using stem", filepath.name)
    return filepath.stem


def read_single_csv(filepath: Path) -> pd.DataFrame | None:
    """Read a single semicolon-separated CSV, returning a wide DataFrame or None on failure."""
    try:
        df = pd.read_csv(
            filepath,
            sep=";",
            encoding="utf-8",
            on_bad_lines="warn",
            engine="python",
        )
    except UnicodeDecodeError:
        logger.warning("UTF-8 failed for %s, retrying with latin-1", filepath.name)
        try:
            df = pd.read_csv(
                filepath,
                sep=";",
                encoding="latin-1",
                on_bad_lines="warn",
                engine="python",
            )
        except Exception as exc:
            logger.error("Failed to read %s: %s", filepath.name, exc)
            return None
    except Exception as exc:
        logger.error("Failed to read %s: %s", filepath.name, exc)
        return None

    if df.empty or "Temps" not in df.columns:
        logger.warning("Skipping %s: empty or missing Temps column", filepath.name)
        return None

    return df


def unpivot_channels(wide_df: pd.DataFrame, device: str, source_file: str) -> pd.DataFrame:
    """Convert wide 8-channel row format to long format with one row per (timestamp, channel)."""
    rows = []

    # Parse timestamp once
    wide_df["Temps"] = pd.to_datetime(wide_df["Temps"], errors="coerce")
    valid_mask = wide_df["Temps"].notna()
    if not valid_mask.all():
        n_bad = (~valid_mask).sum()
        logger.warning("Dropped %d rows with unparseable timestamps in %s", n_bad, source_file)
        wide_df = wide_df.loc[valid_mask].copy()

    for ch in range(1, NUM_CHANNELS + 1):
        ch_df = pd.DataFrame()
        ch_df["timestamp"] = wide_df["Temps"]
        ch_df["device"] = device
        ch_df["source_file"] = source_file
        ch_df["channel"] = ch

        for target_col, csv_prefix in CHANNEL_GROUPS.items():
            src_col = f"{csv_prefix}_{ch}"
            if src_col in wide_df.columns:
                if target_col == "switch":
                    # Convert ON/OFF to boolean
                    ch_df[target_col] = wide_df[src_col].map(
                        {"ON": True, "OFF": False}
                    )
                else:
                    ch_df[target_col] = pd.to_numeric(wide_df[src_col], errors="coerce")
            else:
                logger.warning("Missing column %s in %s", src_col, source_file)
                ch_df[target_col] = pd.NA

        rows.append(ch_df)

    if not rows:
        return pd.DataFrame()

    return pd.concat(rows, ignore_index=True)


def parse_all_csvs(input_dir: Path) -> pd.DataFrame:
    """Parse all CSV files in directory, return consolidated long-format DataFrame."""
    csv_files = sorted(input_dir.glob("*.csv"))
    if not csv_files:
        logger.error("No CSV files found in %s", input_dir)
        sys.exit(1)

    logger.info("Found %d CSV files in %s", len(csv_files), input_dir)

    all_frames = []
    files_ok = 0
    files_err = 0

    for csv_path in csv_files:
        device = extract_device_name(csv_path)
        wide_df = read_single_csv(csv_path)

        if wide_df is None:
            files_err += 1
            continue

        long_df = unpivot_channels(wide_df, device, csv_path.name)
        if long_df.empty:
            files_err += 1
            continue

        all_frames.append(long_df)
        files_ok += 1
        logger.debug("Parsed %s: %d rows (%s)", csv_path.name, len(long_df), device)

    if not all_frames:
        logger.error("No data could be parsed from any file")
        sys.exit(1)

    logger.info("Successfully parsed %d/%d files (%d errors)", files_ok, files_ok + files_err, files_err)

    consolidated = pd.concat(all_frames, ignore_index=True)
    consolidated.sort_values(["device", "channel", "timestamp"], inplace=True)
    consolidated.reset_index(drop=True, inplace=True)

    return consolidated


def print_summary(df: pd.DataFrame) -> None:
    """Log summary statistics of the consolidated DataFrame."""
    logger.info("=== Consolidated Dataset Summary ===")
    logger.info("Total rows: %d", len(df))
    logger.info("Devices: %s", sorted(df["device"].unique()))
    logger.info("Channels: %s", sorted(df["channel"].unique()))
    logger.info(
        "Time range: %s -> %s",
        df["timestamp"].min(),
        df["timestamp"].max(),
    )
    logger.info("Source files: %d unique", df["source_file"].nunique())

    for col in ["voltage", "current", "ah_cons", "ah_charge"]:
        if col in df.columns:
            valid = df[col].dropna()
            logger.info(
                "%s: count=%d, min=%.3f, mean=%.3f, max=%.3f, std=%.3f",
                col,
                len(valid),
                valid.min(),
                valid.mean(),
                valid.max(),
                valid.std(),
            )

    # Per-device row counts
    logger.info("--- Per-device row counts ---")
    for device, count in df.groupby("device").size().items():
        logger.info("  %s: %d rows", device, count)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Parse KXKM Parallelator CSV logs into consolidated parquet"
    )
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("hardware/log-sd"),
        help="Directory containing CSV log files (default: 'hardware/log-sd')",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/consolidated.parquet"),
        help="Output parquet file path (default: data/consolidated.parquet)",
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

    input_dir = args.input.resolve()
    output_path = args.output.resolve()

    if not input_dir.is_dir():
        logger.error("Input directory does not exist: %s", input_dir)
        sys.exit(1)

    logger.info("Input directory: %s", input_dir)
    logger.info("Output file: %s", output_path)

    df = parse_all_csvs(input_dir)
    print_summary(df)

    # Ensure output directory exists
    output_path.parent.mkdir(parents=True, exist_ok=True)

    df.to_parquet(output_path, engine="pyarrow", index=False)
    size_mb = output_path.stat().st_size / (1024 * 1024)
    logger.info("Saved consolidated parquet: %s (%.2f MB)", output_path, size_mb)


if __name__ == "__main__":
    main()
