#!/usr/bin/env bash
set -euo pipefail

PYTHON_BIN="${PYTHON_BIN:-python3}"
PIP_BIN="${PIP_BIN:-pip3}"

if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
  echo "[install] Python introuvable: $PYTHON_BIN" >&2
  exit 1
fi

echo "[install] Python: $($PYTHON_BIN --version)"

if command -v pio >/dev/null 2>&1; then
  echo "[install] PlatformIO deja installe: $(pio --version)"
  exit 0
fi

echo "[install] Installation PlatformIO via $PIP_BIN"
"$PIP_BIN" install --upgrade pip
"$PIP_BIN" install platformio

echo "[install] PlatformIO installe: $(pio --version)"
