#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <environment-id>" >&2
  exit 2
fi

env_id="$1"

case "$env_id" in
  sim-host)
    echo "[qa] Running sim-host tests"
    pio test -e sim-host -vv
    ;;
  kxkm-s3-build)
    echo "[qa] Running firmware build for kxkm-s3-16MB"
    pio run -e kxkm-s3-16MB
    ;;
  kxkm-v3-build)
    echo "[qa] Running firmware build for kxkm-v3-16MB"
    pio run -e kxkm-v3-16MB
    ;;
  kxkm-s3-memory-budget)
    echo "[qa] Running memory budget gate for kxkm-s3-16MB"
    scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85
    ;;
  *)
    echo "[qa] Environment inconnu: $env_id" >&2
    exit 3
    ;;
esac

echo "[qa] OK for environment: $env_id"
