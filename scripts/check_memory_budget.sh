#!/usr/bin/env bash
set -euo pipefail

ENV_NAME="kxkm-v3-16MB"
RAM_MAX="75.0"
FLASH_MAX="85.0"
LOG_FILE=""

usage() {
  cat <<EOF
Usage: $(basename "$0") [--env <name>] [--ram-max <percent>] [--flash-max <percent>] [--log <file>]

Examples:
  $(basename "$0")
  $(basename "$0") --env esp-wrover-kit --ram-max 70 --flash-max 80
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --env) ENV_NAME="${2:-}"; shift 2;;
    --ram-max) RAM_MAX="${2:-}"; shift 2;;
    --flash-max) FLASH_MAX="${2:-}"; shift 2;;
    --log) LOG_FILE="${2:-}"; shift 2;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown argument: $1" >&2; usage; exit 2;;
  esac
done

if [[ -z "$LOG_FILE" ]]; then
  LOG_FILE="$(mktemp -t kxkm_memory_budget.XXXXXX.log)"
fi

echo "[memory] env=${ENV_NAME} ram_max=${RAM_MAX}% flash_max=${FLASH_MAX}%"
echo "[memory] log=${LOG_FILE}"

if command -v pio >/dev/null 2>&1; then
  pio run -e "$ENV_NAME" 2>&1 | tee "$LOG_FILE"
elif command -v /Users/electron/.local/bin/pio >/dev/null 2>&1; then
  /Users/electron/.local/bin/pio run -e "$ENV_NAME" 2>&1 | tee "$LOG_FILE"
else
  python3 -m platformio run -e "$ENV_NAME" 2>&1 | tee "$LOG_FILE"
fi

RAM_USED="$(awk '/^[[:space:]]*RAM:/{for(i=1;i<=NF;i++){if($i ~ /^[0-9]+(\.[0-9]+)?%$/){gsub("%","",$i); print $i; exit}}}' "$LOG_FILE")"
FLASH_USED="$(awk '/^[[:space:]]*Flash:/{for(i=1;i<=NF;i++){if($i ~ /^[0-9]+(\.[0-9]+)?%$/){gsub("%","",$i); print $i; exit}}}' "$LOG_FILE")"

if [[ -z "${RAM_USED:-}" || -z "${FLASH_USED:-}" ]]; then
  echo "[memory] unable to parse RAM/Flash usage from build output." >&2
  exit 1
fi

ram_ok="$(awk -v used="$RAM_USED" -v max="$RAM_MAX" 'BEGIN{print (used <= max) ? "1" : "0"}')"
flash_ok="$(awk -v used="$FLASH_USED" -v max="$FLASH_MAX" 'BEGIN{print (used <= max) ? "1" : "0"}')"

echo "[memory] RAM=${RAM_USED}% (max ${RAM_MAX}%)"
echo "[memory] Flash=${FLASH_USED}% (max ${FLASH_MAX}%)"

if [[ "$ram_ok" != "1" || "$flash_ok" != "1" ]]; then
  echo "[memory] budget check FAILED" >&2
  exit 1
fi

echo "[memory] budget check PASSED"
