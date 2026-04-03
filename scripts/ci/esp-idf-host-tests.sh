#!/usr/bin/env bash
set -euo pipefail

echo "=== ESP-IDF Host Tests ==="

cd "$(dirname "$0")/../../firmware-idf/test"

make clean
make all

echo "=== All ESP-IDF host tests passed ==="
