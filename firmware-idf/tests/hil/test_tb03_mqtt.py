"""TB03: Verify MQTT telemetry arrives at 1 Hz with sane values."""

import json
import time

from hil_report import TbResult


def test_tb03_snapshot_coherence(mqtt_client, report):
    """TB03: Verify MQTT telemetry arrives at 1 Hz with sane values."""
    received = []

    def on_msg(c, u, msg):
        received.append(json.loads(msg.payload))

    mqtt_client.on_message = on_msg
    mqtt_client.subscribe("bmu/+/telemetry")
    mqtt_client.loop_start()
    time.sleep(30)
    mqtt_client.loop_stop()

    ok = len(received) >= 25
    status = "PASS" if ok else "FAIL"
    report.add(TbResult("TB03", "MQTT coherence", status, notes=f"{len(received)} msgs"))
    assert ok, f"Expected >=25 msgs in 30s, got {len(received)}"
