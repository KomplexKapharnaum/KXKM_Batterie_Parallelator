"""Pytest fixtures for BMU HIL tests."""

from __future__ import annotations

import os
import subprocess

import paho.mqtt.client as mqtt
import pytest

from hil_device import HilDevice
from hil_report import HilReport


# ------------------------------------------------------------------
# Helpers
# ------------------------------------------------------------------

def _firmware_sha() -> str:
    """Return the short git SHA of the current firmware build."""
    try:
        result = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            capture_output=True,
            text=True,
            cwd=os.path.join(os.path.dirname(__file__), "..", ".."),
        )
        return result.stdout.strip() or "unknown"
    except Exception:
        return "unknown"


# ------------------------------------------------------------------
# Fixtures
# ------------------------------------------------------------------

@pytest.fixture(scope="session")
def hil_device():
    """Session-scoped serial device wrapper."""
    port = os.environ.get("BMU_HIL_PORT", "/dev/cu.usbmodem1101")
    baud = int(os.environ.get("BMU_HIL_BAUD", "115200"))
    device = HilDevice(port=port, baud=baud)
    yield device
    device.close()


@pytest.fixture(scope="session")
def mqtt_client():
    """Session-scoped MQTT client connected to the broker."""
    broker = os.environ.get("BMU_HIL_BROKER", "kxkm-ai")
    port = int(os.environ.get("BMU_HIL_MQTT_PORT", "1883"))

    client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        client_id="hil-test-runner",
    )
    client.connect(broker, port, keepalive=60)
    yield client
    client.disconnect()


@pytest.fixture(scope="session")
def report():
    """Session-scoped HIL report — renders PDF on teardown."""
    rpt = HilReport(
        device_id=os.environ.get("BMU_DEVICE_ID", "KXKM-BMU-001"),
        firmware_sha=_firmware_sha(),
        tester=os.environ.get("BMU_HIL_TESTER", "auto"),
    )
    yield rpt
    pdf = rpt.render_pdf()
    print(f"\n[HIL] Report written to {pdf.resolve()}")
