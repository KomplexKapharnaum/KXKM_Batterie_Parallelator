#!/usr/bin/env bash
set -euo pipefail

REMOTE_HOST="${1:-kxkm@kxkm-ai}"
REMOTE_DIR="${2:-~/services/kxkm-mqtt-azure-bridge}"

echo "[INFO] Verification Docker existant sur ${REMOTE_HOST}"
ssh "${REMOTE_HOST}" '
  docker ps -a --format "{{.Names}}" | grep -Ei "mqtt|mosquitto|azure.*bridge|iot" || true
'

echo "[INFO] Deploiement bridge Python (sans nouveau conteneur Docker)"
ssh "${REMOTE_HOST}" "mkdir -p ${REMOTE_DIR}"
scp "$(dirname "$0")/requirements.txt" "${REMOTE_HOST}:${REMOTE_DIR}/requirements.txt"
scp "$(dirname "$0")/mqtt_to_azure_iothub_bridge.py" "${REMOTE_HOST}:${REMOTE_DIR}/mqtt_to_azure_iothub_bridge.py"

ssh "${REMOTE_HOST}" "
  python3 -m venv ${REMOTE_DIR}/.venv
  source ${REMOTE_DIR}/.venv/bin/activate
  pip install --upgrade pip
  pip install -r ${REMOTE_DIR}/requirements.txt
  chmod +x ${REMOTE_DIR}/mqtt_to_azure_iothub_bridge.py
  if [ ! -f ${REMOTE_DIR}/.env ]; then
    cat > ${REMOTE_DIR}/.env <<'EOF'
MQTT_BROKER_HOST=127.0.0.1
MQTT_BROKER_PORT=1883
MQTT_TOPIC=kxkm/bmu/telemetry
MQTT_USERNAME=
MQTT_PASSWORD=
AZURE_IOTHUB_CONNECTION_STRING=
BRIDGE_LOG_LEVEL=INFO
EOF
  fi
"

cat <<'MSG'
[OK] Bridge deploye sur kxkm-ai.

Pour lancer manuellement:
  ssh kxkm@kxkm-ai
  cd ~/services/kxkm-mqtt-azure-bridge
  source .venv/bin/activate
  set -a; source .env; set +a
  python mqtt_to_azure_iothub_bridge.py

Aucune stack Docker MQTT/Azure supplementaire n'a ete creee.
MSG
