#!/usr/bin/env python3
"""Import BMU CSV logs from SD card into InfluxDB on kxkm-ai.

Usage: python3 scripts/import_logs_influx.py [--dry-run]

Reads all CSV files from hardware/log-sd/, extracts BMU name from filename
(datalog_BMUNAME_NNN.csv), converts to InfluxDB line protocol, and writes
to bucket "bmu" with measurement "battery_log".

Tags: bmu (device name), battery_id (1-8)
Fields: voltage (float, V→mV), current (float, A→mA), switch (string ON/OFF),
        ah_discharge (float), ah_charge (float)
"""

import csv
import os
import sys
import re
from datetime import datetime
from pathlib import Path

# InfluxDB config
INFLUX_URL = "http://100.87.54.119:8086"  # kxkm-ai via Tailscale
INFLUX_TOKEN = "kxkm-influx-token-2026"
INFLUX_ORG = "kxkm"
INFLUX_BUCKET = "bmu"

LOG_DIR = Path(__file__).parent.parent / "hardware" / "log-sd"
BATCH_SIZE = 50000


def parse_csv(filepath: Path, bmu_name: str) -> list[str]:
    """Parse one CSV file into InfluxDB line protocol lines."""
    lines = []
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        reader = csv.reader(f, delimiter=";")
        header = next(reader, None)
        if not header or header[0] != "Temps":
            return lines

        for row in reader:
            if len(row) < 43:
                continue
            try:
                ts = datetime.strptime(row[0].strip(), "%Y-%m-%d %H:%M:%S")
                ts_ns = int(ts.timestamp() * 1_000_000_000)
            except (ValueError, IndexError):
                continue

            for i in range(8):
                bat_id = i + 1
                try:
                    volt = float(row[1 + i])
                    curr = float(row[9 + i])
                    switch = row[17 + i].strip()
                    ah_dis = float(row[25 + i])
                    ah_ch = float(row[33 + i])
                except (ValueError, IndexError):
                    continue

                # Convert V→mV for consistency with firmware
                volt_mv = volt * 1000.0
                curr_ma = curr * 1000.0

                line = (
                    f"battery_log,"
                    f"bmu={bmu_name},"
                    f"battery_id={bat_id} "
                    f"v_mv={volt_mv:.0f},"
                    f"i_ma={curr_ma:.1f},"
                    f"switch=\"{switch}\","
                    f"ah_discharge={ah_dis:.4f},"
                    f"ah_charge={ah_ch:.4f} "
                    f"{ts_ns}"
                )
                lines.append(line)

            # Also write totals
            try:
                tot_curr = float(row[41])
                tot_ch = float(row[42])
                tot_cons = float(row[43]) if len(row) > 43 else 0
                line = (
                    f"battery_log_total,"
                    f"bmu={bmu_name} "
                    f"total_current={tot_curr:.3f},"
                    f"total_charge={tot_ch:.4f},"
                    f"total_consumption={tot_cons:.4f} "
                    f"{ts_ns}"
                )
                lines.append(line)
            except (ValueError, IndexError):
                pass

    return lines


def write_to_influx(lines: list[str], dry_run: bool = False):
    """Write line protocol to InfluxDB in batches."""
    import urllib.request

    total = len(lines)
    written = 0

    for i in range(0, total, BATCH_SIZE):
        batch = lines[i:i + BATCH_SIZE]
        body = "\n".join(batch).encode("utf-8")

        if dry_run:
            written += len(batch)
            print(f"  [DRY] {written}/{total} lines")
            continue

        url = f"{INFLUX_URL}/api/v2/write?org={INFLUX_ORG}&bucket={INFLUX_BUCKET}&precision=ns"
        req = urllib.request.Request(url, data=body, method="POST")
        req.add_header("Authorization", f"Token {INFLUX_TOKEN}")
        req.add_header("Content-Type", "text/plain; charset=utf-8")

        try:
            urllib.request.urlopen(req, timeout=30)
            written += len(batch)
            print(f"  {written}/{total} lines written")
        except Exception as e:
            print(f"  ERROR at {written}: {e}")
            # Try to continue with next batch
            written += len(batch)

    return written


def main():
    dry_run = "--dry-run" in sys.argv

    csv_files = sorted(LOG_DIR.glob("datalog_*.csv"))
    if not csv_files:
        print(f"No CSV files found in {LOG_DIR}")
        return

    print(f"Found {len(csv_files)} CSV files in {LOG_DIR}")

    # Group by BMU name
    bmu_files: dict[str, list[Path]] = {}
    for f in csv_files:
        m = re.match(r"datalog_(.+?)_\d+\.csv", f.name)
        if m:
            bmu = m.group(1)
            bmu_files.setdefault(bmu, []).append(f)

    print(f"BMU devices: {', '.join(sorted(bmu_files.keys()))}")
    print()

    total_written = 0
    for bmu_name, files in sorted(bmu_files.items()):
        print(f"═══ {bmu_name.upper()} ({len(files)} files) ═══")
        all_lines = []
        for f in sorted(files):
            lines = parse_csv(f, bmu_name)
            print(f"  {f.name}: {len(lines)} lines")
            all_lines.extend(lines)

        if all_lines:
            print(f"  Total: {len(all_lines)} line protocol entries")
            written = write_to_influx(all_lines, dry_run)
            total_written += written
        print()

    print(f"Done — {total_written} total entries {'(dry run)' if dry_run else 'written to InfluxDB'}")


if __name__ == "__main__":
    main()
