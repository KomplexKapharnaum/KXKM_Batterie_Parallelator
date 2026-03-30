#!/usr/bin/env bash
set -euo pipefail

scripts/ci/install_tooling.sh

for env_id in sim-host kxkm-s3-build kxkm-s3-memory-budget; do
  echo "[qa-all] Start: $env_id"
  scripts/ci/run_qa_env.sh "$env_id"
  echo "[qa-all] Done:  $env_id"
done

echo "[qa-all] All environments passed"
