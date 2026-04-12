"""TB01: Verify boot sequence completes with all subsystems."""

import time

from hil_report import TbResult


def test_tb01_boot_sequence(hil_device, report):
    """TB01: Verify boot sequence completes with all subsystems."""
    hil_device.open()
    hil_device.reboot()
    t0 = time.time()
    assert hil_device.expect("bmu_core_init OK", timeout=15)
    assert hil_device.expect("task_bmu_core", timeout=5)
    assert hil_device.expect("UI init", timeout=5)
    report.add(TbResult("TB01", "Boot sequence", "PASS", duration_s=time.time() - t0))
    hil_device.close()
