"""TB02: Verify I2C scan detects 16 INA237 + 4 TCA9535."""

import re

from hil_report import TbResult


def test_tb02_i2c_scan(hil_device, report):
    """TB02: Verify I2C scan detects 16 INA237 + 4 TCA9535."""
    hil_device.open()
    hil_device.reboot()
    hil_device.capture_for(20)
    log = "\n".join(hil_device.log_lines)
    # Count from I2C scan log line "I2C scan: INA=X TCA=Y"
    m = re.search(r"INA=(\d+)\s+TCA=(\d+)", log)
    n_ina = int(m.group(1)) if m else 0
    n_tca = int(m.group(2)) if m else 0
    status = "PASS" if (n_ina == 16 and n_tca == 4) else "FAIL"
    report.add(TbResult("TB02", "I2C scan", status, notes=f"INA={n_ina} TCA={n_tca}"))
    assert n_ina == 16 and n_tca == 4
    hil_device.close()
