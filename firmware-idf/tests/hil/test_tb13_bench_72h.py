"""TB13: Long-duration stability bench."""

import json
import os
import time
from pathlib import Path

from hil_report import TbResult

BENCH_HOURS = int(os.getenv("BMU_BENCH_HOURS", "72"))


def test_tb13_bench_72h(mqtt_client, report, tmp_path):
    """TB13: Long-duration stability bench."""
    log_path = Path(tmp_path) / "tb13_72h.jsonl"
    log_fp = log_path.open("w")
    mqtt_seen = 0
    reboots = 0
    heap_start = None
    heap_last = None
    t_start = time.time()

    def on_msg(c, u, msg):
        nonlocal mqtt_seen, heap_start, heap_last, reboots
        mqtt_seen += 1
        try:
            data = json.loads(msg.payload)
        except Exception:
            return
        hp = data.get("sy", {}).get("hp", 0)
        up = data.get("sy", {}).get("up", 0)
        if heap_start is None:
            heap_start = hp
        heap_last = hp
        if up < 60 and mqtt_seen > 20:
            reboots += 1
        rec = {"t": time.time(), "up": up, "hp": hp, "n": mqtt_seen}
        log_fp.write(json.dumps(rec) + "\n")
        log_fp.flush()

    mqtt_client.on_message = on_msg
    mqtt_client.subscribe("bmu/+/telemetry")
    mqtt_client.loop_start()

    deadline = t_start + BENCH_HOURS * 3600
    while time.time() < deadline:
        time.sleep(300)
        elapsed_h = (time.time() - t_start) / 3600
        print(f"[{elapsed_h:.1f}h] msgs={mqtt_seen} reboots={reboots} heap={heap_last}")

    mqtt_client.loop_stop()
    log_fp.close()

    heap_delta_pct = (
        abs((heap_last or 0) - (heap_start or 1)) / max(heap_start or 1, 1) * 100
    )
    ok = reboots == 0 and heap_delta_pct < 5
    status = "PASS" if ok else "FAIL"
    notes = f"msgs={mqtt_seen} reboots={reboots} heap_delta={heap_delta_pct:.1f}%"
    report.add(
        TbResult(
            "TB13", f"Bench {BENCH_HOURS}h stability", status,
            notes=notes, duration_s=BENCH_HOURS * 3600,
        )
    )
    assert ok, notes
