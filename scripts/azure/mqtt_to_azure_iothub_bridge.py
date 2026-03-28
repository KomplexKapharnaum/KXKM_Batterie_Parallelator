#!/usr/bin/env python3
"""Bridge MQTT telemetry to Azure IoT Hub without adding a Docker broker.

Designed for kxkm-ai host execution (systemd user service or foreground).
"""

from __future__ import annotations

import json
import logging
import os
import signal
import sys
import threading
import time
from typing import Optional

import paho.mqtt.client as mqtt
from azure.iot.device import IoTHubDeviceClient, Message

LOG_LEVEL = os.getenv("BRIDGE_LOG_LEVEL", "INFO").upper()
logging.basicConfig(
    level=getattr(logging, LOG_LEVEL, logging.INFO),
    format="%(asctime)s %(levelname)s %(message)s",
)

MQTT_BROKER_HOST = os.getenv("MQTT_BROKER_HOST", "127.0.0.1")
MQTT_BROKER_PORT = int(os.getenv("MQTT_BROKER_PORT", "1883"))
MQTT_TOPIC = os.getenv("MQTT_TOPIC", "kxkm/bmu/telemetry")
MQTT_USERNAME = os.getenv("MQTT_USERNAME", "")
MQTT_PASSWORD = os.getenv("MQTT_PASSWORD", "")

AZURE_IOTHUB_CONNECTION_STRING = os.getenv("AZURE_IOTHUB_CONNECTION_STRING", "")

_shutdown = threading.Event()
_iot_client: Optional[IoTHubDeviceClient] = None


def _signal_handler(signum, _frame):
    logging.info("Signal %s recu, arret du bridge", signum)
    _shutdown.set()


def _require_env() -> None:
    if not AZURE_IOTHUB_CONNECTION_STRING:
        logging.error("AZURE_IOTHUB_CONNECTION_STRING manquant")
        sys.exit(2)


def _build_iot_client() -> IoTHubDeviceClient:
    client = IoTHubDeviceClient.create_from_connection_string(
        AZURE_IOTHUB_CONNECTION_STRING
    )
    client.connect()
    logging.info("Connecte a Azure IoT Hub")
    return client


def _forward_payload(payload: bytes) -> None:
    global _iot_client

    if _iot_client is None:
        _iot_client = _build_iot_client()

    # Validate JSON while preserving original payload for IoT Hub.
    decoded = payload.decode("utf-8", errors="strict")
    json.loads(decoded)

    msg = Message(decoded)
    msg.content_type = "application/json"
    msg.content_encoding = "utf-8"
    _iot_client.send_message(msg)


def _on_connect(client: mqtt.Client, _userdata, _flags, rc):
    if rc != 0:
        logging.error("Echec connexion MQTT rc=%s", rc)
        return

    logging.info("Connecte au broker MQTT %s:%s", MQTT_BROKER_HOST, MQTT_BROKER_PORT)
    client.subscribe(MQTT_TOPIC, qos=1)
    logging.info("Subscription topic=%s", MQTT_TOPIC)


def _on_message(_client: mqtt.Client, _userdata, message: mqtt.MQTTMessage):
    try:
        _forward_payload(message.payload)
        logging.info("Forwarded topic=%s bytes=%s", message.topic, len(message.payload))
    except Exception as exc:  # noqa: BLE001
        logging.exception("Erreur forwarding MQTT->Azure: %s", exc)


def main() -> int:
    _require_env()

    signal.signal(signal.SIGINT, _signal_handler)
    signal.signal(signal.SIGTERM, _signal_handler)

    mqtt_client = mqtt.Client(client_id="kxkm-mqtt-azure-bridge")
    if MQTT_USERNAME:
        mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

    mqtt_client.on_connect = _on_connect
    mqtt_client.on_message = _on_message

    mqtt_client.reconnect_delay_set(min_delay=2, max_delay=30)
    mqtt_client.connect_async(MQTT_BROKER_HOST, MQTT_BROKER_PORT, keepalive=30)
    mqtt_client.loop_start()

    try:
        while not _shutdown.is_set():
            time.sleep(0.5)
    finally:
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
        if _iot_client is not None:
            _iot_client.disconnect()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
