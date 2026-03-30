#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <environment-id>" >&2
  exit 2
fi

env_id="$1"

ensure_credentials_for_ci() {
  if [[ -f src/credentials.h ]]; then
    return 0
  fi

  if [[ -f src/credentials.h.example ]]; then
    echo "[qa] credentials.h absent -> generation depuis credentials.h.example"
    cp src/credentials.h.example src/credentials.h
    return 0
  fi

  echo "[qa] credentials.h et credentials.h.example introuvables" >&2
  return 1
}

case "$env_id" in
  sim-host)
    echo "[qa] Running sim-host tests"
    pio test -e sim-host -vv
    ;;
  kxkm-s3-build)
    echo "[qa] Legacy id detected (kxkm-s3-build) -> using kxkm-v3-16MB"
    ensure_credentials_for_ci
    pio run -e kxkm-v3-16MB
    ;;
  kxkm-v3-build)
    echo "[qa] Running firmware build for kxkm-v3-16MB"
    ensure_credentials_for_ci
    pio run -e kxkm-v3-16MB
    ;;
  kxkm-v3-memory-budget)
    echo "[qa] Running memory budget gate for kxkm-v3-16MB"
    ensure_credentials_for_ci
    scripts/check_memory_budget.sh --env kxkm-v3-16MB --ram-max 75 --flash-max 85
    ;;
  kxkm-s3-memory-budget)
    echo "[qa] Legacy id detected (kxkm-s3-memory-budget) -> using kxkm-v3-16MB"
    ensure_credentials_for_ci
    scripts/check_memory_budget.sh --env kxkm-v3-16MB --ram-max 75 --flash-max 85
    ;;
  *)
    echo "[qa] Environment inconnu: $env_id" >&2
    exit 3
    ;;
esac

echo "[qa] OK for environment: $env_id"
