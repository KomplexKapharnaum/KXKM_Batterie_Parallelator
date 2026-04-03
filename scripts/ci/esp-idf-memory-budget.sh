#!/usr/bin/env bash
set -euo pipefail

FLASH_MAX=${1:-85}
PARTITION_SIZE=$((0x200000))

cd "$(dirname "$0")/../../firmware-idf"

echo "=== ESP-IDF Memory Budget Check ==="

BIN="build/kxkm-bmu.bin"
if [ ! -f "$BIN" ]; then
    echo "ERROR: $BIN not found. Run 'idf.py build' first."
    exit 1
fi

# Cross-platform stat
if stat -f%z "$BIN" >/dev/null 2>&1; then
    BIN_SIZE=$(stat -f%z "$BIN")
else
    BIN_SIZE=$(stat -c%s "$BIN")
fi

PERCENT=$(( BIN_SIZE * 100 / PARTITION_SIZE ))

echo "Binary: $BIN_SIZE bytes ($PERCENT% of ${PARTITION_SIZE} byte partition)"
echo "Threshold: ${FLASH_MAX}%"

if [ "$PERCENT" -gt "$FLASH_MAX" ]; then
    echo "FAIL: Flash usage ${PERCENT}% exceeds max ${FLASH_MAX}%"
    exit 1
fi

echo "PASS: Flash usage ${PERCENT}% <= ${FLASH_MAX}%"
echo "=== Memory budget OK ==="
