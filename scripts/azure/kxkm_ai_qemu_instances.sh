#!/usr/bin/env bash
set -euo pipefail

# Gestion d'instances QEMU sur l'hote distant kxkm-ai.
# Usage:
#   ./scripts/azure/kxkm_ai_qemu_instances.sh provision
#   ./scripts/azure/kxkm_ai_qemu_instances.sh start
#   ./scripts/azure/kxkm_ai_qemu_instances.sh status
#   ./scripts/azure/kxkm_ai_qemu_instances.sh stop
#   ./scripts/azure/kxkm_ai_qemu_instances.sh destroy

COMMAND="${1:-help}"
REMOTE_HOST="${KXKM_AI_HOST:-kxkm@kxkm-ai}"
REMOTE_BASE_DIR="${KXKM_QEMU_BASE_DIR:-~/services/kxkm-qemu}"
INSTANCE_PREFIX="${KXKM_QEMU_PREFIX:-kxkm-ai-qemu}"
INSTANCE_COUNT="${KXKM_QEMU_COUNT:-2}"
FIRST_SSH_PORT="${KXKM_QEMU_FIRST_SSH_PORT:-22220}"
RAM_MB="${KXKM_QEMU_RAM_MB:-2048}"
CPUS="${KXKM_QEMU_CPUS:-2}"
DISK_GB="${KXKM_QEMU_DISK_GB:-20}"
IMAGE_URL="${KXKM_QEMU_IMAGE_URL:-https://cloud-images.ubuntu.com/noble/current/noble-server-cloudimg-amd64.img}"

usage() {
  cat <<'EOF'
Gestion QEMU sur kxkm-ai

Commandes:
  provision  : prepare image de base + overlays + cloud-init
  start      : lance toutes les instances configurees
  stop       : arrete toutes les instances configurees
  status     : affiche l'etat et les ports SSH forwards
  destroy    : stop + suppression des instances (pas l'image de base)
  help       : affiche cette aide

Variables d'environnement utiles:
  KXKM_AI_HOST                 (defaut: kxkm@kxkm-ai)
  KXKM_QEMU_BASE_DIR           (defaut: ~/services/kxkm-qemu)
  KXKM_QEMU_PREFIX             (defaut: kxkm-ai-qemu)
  KXKM_QEMU_COUNT              (defaut: 2)
  KXKM_QEMU_FIRST_SSH_PORT     (defaut: 22220)
  KXKM_QEMU_RAM_MB             (defaut: 2048)
  KXKM_QEMU_CPUS               (defaut: 2)
  KXKM_QEMU_DISK_GB            (defaut: 20)
  KXKM_QEMU_IMAGE_URL          (defaut: Ubuntu Noble cloud image)
  KXKM_QEMU_SSH_PUBKEY_PATH    (defaut: auto-detection ~/.ssh/*.pub)
EOF
}

resolve_pubkey() {
  if [[ -n "${KXKM_QEMU_SSH_PUBKEY_PATH:-}" ]]; then
    cat "${KXKM_QEMU_SSH_PUBKEY_PATH}"
    return
  fi

  local candidates=(
    "${HOME}/.ssh/id_ed25519.pub"
    "${HOME}/.ssh/id_rsa.pub"
    "${HOME}/.ssh/id_ecdsa.pub"
  )
  local key
  for key in "${candidates[@]}"; do
    if [[ -f "${key}" ]]; then
      cat "${key}"
      return
    fi
  done

  return 1
}

run_remote() {
  local ssh_pubkey="$1"
  local remote_cmd="$2"
  local ssh_pubkey_b64
  ssh_pubkey_b64="$(printf '%s' "${ssh_pubkey}" | base64 | tr -d '\n')"

  ssh "${REMOTE_HOST}" \
    BASE_DIR="${REMOTE_BASE_DIR}" \
    PREFIX="${INSTANCE_PREFIX}" \
    COUNT="${INSTANCE_COUNT}" \
    FIRST_PORT="${FIRST_SSH_PORT}" \
    RAM_MB="${RAM_MB}" \
    CPUS="${CPUS}" \
    DISK_GB="${DISK_GB}" \
    IMAGE_URL="${IMAGE_URL}" \
    SSH_PUBKEY_B64="${ssh_pubkey_b64}" \
    'bash -se' <<'REMOTE_SCRIPT'
set -euo pipefail

base_dir="${BASE_DIR/#\~/$HOME}"
prefix="${PREFIX}"
count="${COUNT}"
first_port="${FIRST_PORT}"
ram_mb="${RAM_MB}"
cpus="${CPUS}"
disk_gb="${DISK_GB}"
image_url="${IMAGE_URL}"
ssh_pubkey="$(printf '%s' "${SSH_PUBKEY_B64}" | base64 -d)"
image_name="$(basename "${image_url}")"

instances_dir="${base_dir}/instances"
images_dir="${base_dir}/images"
logs_dir="${base_dir}/logs"

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "[ERREUR] Commande manquante: $1" >&2
    exit 1
  }
}

ensure_tools() {
  if command -v apt-get >/dev/null 2>&1; then
    if ! command -v qemu-system-x86_64 >/dev/null 2>&1 || \
       ! command -v qemu-img >/dev/null 2>&1 || \
       ! command -v cloud-localds >/dev/null 2>&1; then
      echo "[INFO] Installation dependances QEMU (sudo requis si absent)."
      sudo apt-get update -y
      sudo apt-get install -y qemu-system-x86 qemu-utils cloud-image-utils curl
    fi
  fi

  need_cmd qemu-system-x86_64
  need_cmd qemu-img
  need_cmd cloud-localds
  need_cmd curl
}

provision_all() {
  ensure_tools
  mkdir -p "${instances_dir}" "${images_dir}" "${logs_dir}"

  if [[ ! -f "${images_dir}/${image_name}" ]]; then
    echo "[INFO] Telechargement image cloud: ${image_url}"
    curl -fL "${image_url}" -o "${images_dir}/${image_name}"
  else
    echo "[INFO] Image deja presente: ${images_dir}/${image_name}"
  fi

  for i in $(seq 1 "${count}"); do
    name="${prefix}-${i}"
    ssh_port=$((first_port + i - 1))
    inst_dir="${instances_dir}/${name}"
    mkdir -p "${inst_dir}"

    if [[ ! -f "${inst_dir}/disk.qcow2" ]]; then
      qemu-img create -f qcow2 -F qcow2 -b "${images_dir}/${image_name}" "${inst_dir}/disk.qcow2" "${disk_gb}G" >/dev/null
    fi

    cat > "${inst_dir}/user-data" <<EOF
#cloud-config
users:
  - default
  - name: kxkm
    groups: [sudo]
    shell: /bin/bash
    sudo: ALL=(ALL) NOPASSWD:ALL
    ssh_authorized_keys:
      - ${ssh_pubkey}
package_update: true
packages:
  - qemu-guest-agent
EOF

    cat > "${inst_dir}/meta-data" <<EOF
instance-id: ${name}
local-hostname: ${name}
EOF

    cloud-localds "${inst_dir}/seed.iso" "${inst_dir}/user-data" "${inst_dir}/meta-data"

    cat > "${inst_dir}/run.conf" <<EOF
NAME=${name}
SSH_PORT=${ssh_port}
RAM_MB=${ram_mb}
CPUS=${cpus}
EOF

    echo "[OK] provision ${name} (ssh localhost:${ssh_port})"
  done
}

start_all() {
  mkdir -p "${logs_dir}"
  for i in $(seq 1 "${count}"); do
    name="${prefix}-${i}"
    inst_dir="${instances_dir}/${name}"
    pid_file="${inst_dir}/qemu.pid"
    log_file="${logs_dir}/${name}.log"

    if [[ ! -f "${inst_dir}/run.conf" ]]; then
      echo "[ERREUR] ${name} non provisionnee. Lance d'abord 'provision'." >&2
      exit 1
    fi

    # shellcheck disable=SC1090
    source "${inst_dir}/run.conf"

    if [[ -f "${pid_file}" ]] && kill -0 "$(cat "${pid_file}")" 2>/dev/null; then
      echo "[INFO] ${name} deja lance (pid $(cat "${pid_file}"))"
      continue
    fi

    nohup qemu-system-x86_64 \
      -name "${NAME}" \
      -machine accel=tcg \
      -m "${RAM_MB}" \
      -smp "${CPUS}" \
      -cpu max \
      -drive "if=virtio,file=${inst_dir}/disk.qcow2,format=qcow2" \
      -drive "if=virtio,file=${inst_dir}/seed.iso,format=raw" \
      -netdev "user,id=net0,hostfwd=tcp::${SSH_PORT}-:22" \
      -device virtio-net-pci,netdev=net0 \
      -display none \
      -serial none \
      -daemonize \
      -pidfile "${pid_file}" >>"${log_file}" 2>&1

    echo "[OK] start ${name} (ssh kxkm@localhost -p ${SSH_PORT})"
  done
}

stop_all() {
  for i in $(seq 1 "${count}"); do
    name="${prefix}-${i}"
    inst_dir="${instances_dir}/${name}"
    pid_file="${inst_dir}/qemu.pid"

    if [[ -f "${pid_file}" ]]; then
      pid="$(cat "${pid_file}")"
      if kill -0 "${pid}" 2>/dev/null; then
        kill "${pid}"
        echo "[OK] stop ${name} (pid ${pid})"
      else
        echo "[INFO] ${name}: pid stale (${pid})"
      fi
      rm -f "${pid_file}"
    else
      echo "[INFO] ${name}: deja arrete"
    fi
  done
}

status_all() {
  for i in $(seq 1 "${count}"); do
    name="${prefix}-${i}"
    inst_dir="${instances_dir}/${name}"
    pid_file="${inst_dir}/qemu.pid"

    if [[ -f "${inst_dir}/run.conf" ]]; then
      # shellcheck disable=SC1090
      source "${inst_dir}/run.conf"
      if [[ -f "${pid_file}" ]] && kill -0 "$(cat "${pid_file}")" 2>/dev/null; then
        echo "[RUNNING] ${name} pid=$(cat "${pid_file}") ssh=localhost:${SSH_PORT}"
      else
        echo "[STOPPED] ${name} ssh=localhost:${SSH_PORT}"
      fi
    else
      echo "[ABSENT] ${name} (non provisionnee)"
    fi
  done
}

destroy_all() {
  stop_all
  for i in $(seq 1 "${count}"); do
    name="${prefix}-${i}"
    rm -rf "${instances_dir}/${name}"
    echo "[OK] destroy ${name}"
  done
}

case "${remote_cmd:-}" in
  provision) provision_all ;;
  start) start_all ;;
  stop) stop_all ;;
  status) status_all ;;
  destroy) destroy_all ;;
  *)
    echo "[ERREUR] commande distante inconnue: ${remote_cmd:-}" >&2
    exit 1
    ;;
esac
REMOTE_SCRIPT
}

case "${COMMAND}" in
  provision|start|stop|status|destroy)
    if ! ssh_pubkey="$(resolve_pubkey)"; then
      echo "[ERREUR] Cle publique introuvable. Definis KXKM_QEMU_SSH_PUBKEY_PATH." >&2
      exit 1
    fi

    run_remote "${ssh_pubkey}" "${COMMAND}"
    ;;
  help|-h|--help)
    usage
    ;;
  *)
    echo "[ERREUR] Commande inconnue: ${COMMAND}" >&2
    usage
    exit 1
    ;;
esac