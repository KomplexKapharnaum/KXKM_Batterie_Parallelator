#!/usr/bin/env bash
set -euo pipefail

# Remote ML orchestration for KXKM AI host.
# Usage:
#   scripts/ml/remote_kxkm_ai_pipeline.sh check
#   scripts/ml/remote_kxkm_ai_pipeline.sh run --project-dir /home/kxkm/KXKM_Batterie_Parallelator
#   scripts/ml/remote_kxkm_ai_pipeline.sh bootstrap-container
#   scripts/ml/remote_kxkm_ai_pipeline.sh run-container
#   scripts/ml/remote_kxkm_ai_pipeline.sh discover-dataset
#   scripts/ml/remote_kxkm_ai_pipeline.sh inject-dataset --dataset-path /abs/path/on/host/models/consolidated.parquet

REMOTE_HOST="${KXKM_AI_HOST:-kxkm@kxkm-ai}"
PROJECT_DIR="${KXKM_AI_PROJECT_DIR:-/home/kxkm/KXKM_Batterie_Parallelator}"
CONTAINER_NAME="${KXKM_AI_CONTAINER:-mascarade-platformio}"
CONTAINER_PROJECT_DIR="${KXKM_AI_CONTAINER_PROJECT_DIR:-/workspace/KXKM_Batterie_Parallelator}"
DATASET_PATH="${KXKM_AI_DATASET_PATH:-}"
SSH_OPTS=( -o BatchMode=yes -o ConnectTimeout=10 )

usage() {
  cat <<'EOF'
Usage:
  remote_kxkm_ai_pipeline.sh check
  remote_kxkm_ai_pipeline.sh run [--project-dir <remote_path>]
  remote_kxkm_ai_pipeline.sh bootstrap-container [--container <name>] [--container-project-dir <path>]
  remote_kxkm_ai_pipeline.sh run-container [--container <name>] [--container-project-dir <path>]
  remote_kxkm_ai_pipeline.sh discover-dataset [--container <name>]
  remote_kxkm_ai_pipeline.sh inject-dataset --dataset-path <remote_host_abs_path> [--container <name>] [--container-project-dir <path>]

Environment variables:
  KXKM_AI_HOST         Remote SSH target (default: kxkm@kxkm-ai)
  KXKM_AI_PROJECT_DIR  Remote project directory
  KXKM_AI_CONTAINER    Existing remote container name (default: mascarade-platformio)
  KXKM_AI_CONTAINER_PROJECT_DIR  Project directory inside container
  KXKM_AI_DATASET_PATH Absolute path on remote host for consolidated.parquet
EOF
}

check_remote() {
  ssh "${SSH_OPTS[@]}" "${REMOTE_HOST}" "
    set -euo pipefail
    echo HOST=\$(hostname)
    command -v docker >/dev/null && echo DOCKER=ok || echo DOCKER=missing
    docker ps --format 'table {{.Names}}\t{{.Image}}\t{{.Status}}' | sed -n '1,20p'
  "
}

run_remote_pipeline() {
  ssh "${SSH_OPTS[@]}" "${REMOTE_HOST}" "
    set -euo pipefail
    cd '${PROJECT_DIR}'
    python3 --version
    test -f models/consolidated.parquet
    python3 scripts/ml/extract_features.py --input models/consolidated.parquet --output models/features_v2.parquet --window 60 --log-level INFO
    python3 scripts/ml/adapt_features.py --input models/features_v2.parquet --output models/features_adapted_v2.parquet
    python3 scripts/ml/train_fpnn.py --input models/features_adapted_v2.parquet --output-dir models --epochs 50 --hidden 64 --degree 2 --soh-mode capacity --test-device k-led1 --val-ratio 0.1 --qat
    python3 scripts/ml/quantize_tflite.py --model models/fpnn_soh.pt --features models/features_adapted_v2.parquet --output models/fpnn_soh_v2_quantized.onnx --backend onnxrt --calib-samples 2000 --calib-strategy stratified --percentile-clip 1 99
    python3 scripts/ml/finalize_phase2_metrics.py --features models/features_adapted_v2.parquet --quantized models/fpnn_soh_v2_quantized.onnx --rul-model models/rul_sambamixer.pt --train-log phase2_fpnn_train_v2.log --output models/phase2_metrics.json
    ls -lh models/phase2_metrics.json models/fpnn_soh_v2_quantized.onnx
  "
}

check_container_python_deps() {
  ssh "${SSH_OPTS[@]}" "${REMOTE_HOST}" "
    set -euo pipefail
    docker exec '${CONTAINER_NAME}' python3 -c \"import importlib.util as u;mods=['pandas','numpy','torch','onnxruntime','pyarrow','onnx'];[print(m+':' + ('ok' if u.find_spec(m) else 'missing')) for m in mods]\"
  "
}

bootstrap_container_repo() {
  # Sync tracked files only to avoid transferring local build artifacts.
  git archive --format=tar HEAD | ssh "${SSH_OPTS[@]}" "${REMOTE_HOST}" "docker exec -i '${CONTAINER_NAME}' bash -lc 'mkdir -p \"${CONTAINER_PROJECT_DIR}\" && tar -C \"${CONTAINER_PROJECT_DIR}\" -xf - && echo SYNC_OK'"
}

run_container_pipeline() {
  ssh "${SSH_OPTS[@]}" "${REMOTE_HOST}" "
    set -euo pipefail
    docker exec '${CONTAINER_NAME}' bash -lc '
      set -euo pipefail
      cd "${CONTAINER_PROJECT_DIR}"
      python3 --version
      if [[ ! -f models/consolidated.parquet ]]; then
        echo "BLOCKED: models/consolidated.parquet is missing in container workspace" >&2
        exit 2
      fi
      python3 scripts/ml/extract_features.py --input models/consolidated.parquet --output models/features_v2.parquet --window 60 --log-level INFO
      python3 scripts/ml/adapt_features.py --input models/features_v2.parquet --output models/features_adapted_v2.parquet
      python3 scripts/ml/train_fpnn.py --input models/features_adapted_v2.parquet --output-dir models --epochs 50 --hidden 64 --degree 2 --soh-mode capacity --test-device k-led1 --val-ratio 0.1 --qat
      python3 scripts/ml/quantize_tflite.py --model models/fpnn_soh.pt --features models/features_adapted_v2.parquet --output models/fpnn_soh_v2_quantized.onnx --backend onnxrt --calib-samples 2000 --calib-strategy stratified --percentile-clip 1 99
      python3 scripts/ml/finalize_phase2_metrics.py --features models/features_adapted_v2.parquet --quantized models/fpnn_soh_v2_quantized.onnx --rul-model models/rul_sambamixer.pt --train-log phase2_fpnn_train_v2.log --output models/phase2_metrics.json
      ls -lh models/phase2_metrics.json models/fpnn_soh_v2_quantized.onnx
    '
  "
}

discover_dataset() {
  ssh "${SSH_OPTS[@]}" "${REMOTE_HOST}" "
    set -euo pipefail
    echo '# Host-side candidates (first 20)'
    find /home /data /srv -type f -name 'consolidated.parquet' 2>/dev/null | sed -n '1,20p' || true
    echo '# Container-side target check'
    docker exec '${CONTAINER_NAME}' bash -lc '
      set -euo pipefail
      if [[ -f "${CONTAINER_PROJECT_DIR}/models/consolidated.parquet" ]]; then
        echo "CONTAINER_DATASET_PRESENT:${CONTAINER_PROJECT_DIR}/models/consolidated.parquet"
      else
        echo "CONTAINER_DATASET_MISSING:${CONTAINER_PROJECT_DIR}/models/consolidated.parquet"
      fi
    '
  "
}

inject_dataset() {
  if [[ -z "${DATASET_PATH}" ]]; then
    echo "Missing --dataset-path (absolute path on remote host)" >&2
    exit 1
  fi

  ssh "${SSH_OPTS[@]}" "${REMOTE_HOST}" "
    set -euo pipefail
    test -f '${DATASET_PATH}'
    docker exec -i '${CONTAINER_NAME}' bash -lc 'mkdir -p "${CONTAINER_PROJECT_DIR}/models"'
    cat '${DATASET_PATH}' | docker exec -i '${CONTAINER_NAME}' bash -lc 'cat > "${CONTAINER_PROJECT_DIR}/models/consolidated.parquet"'
    docker exec '${CONTAINER_NAME}' bash -lc 'ls -lh "${CONTAINER_PROJECT_DIR}/models/consolidated.parquet"'
  "
}

MODE="${1:-}"
if [[ -z "${MODE}" ]]; then
  usage
  exit 1
fi

if [[ "${MODE}" == "--help" || "${MODE}" == "-h" ]]; then
  usage
  exit 0
fi

shift || true

while [[ $# -gt 0 ]]; do
  case "$1" in
    --project-dir)
      PROJECT_DIR="$2"
      shift 2
      ;;
    --container)
      CONTAINER_NAME="$2"
      shift 2
      ;;
    --container-project-dir)
      CONTAINER_PROJECT_DIR="$2"
      shift 2
      ;;
    --dataset-path)
      DATASET_PATH="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

case "${MODE}" in
  check)
    check_remote
    ;;
  run)
    run_remote_pipeline
    ;;
  bootstrap-container)
    check_container_python_deps
    bootstrap_container_repo
    ;;
  run-container)
    run_container_pipeline
    ;;
  discover-dataset)
    discover_dataset
    ;;
  inject-dataset)
    inject_dataset
    ;;
  *)
    echo "Unknown mode: ${MODE}" >&2
    usage
    exit 1
    ;;
esac
