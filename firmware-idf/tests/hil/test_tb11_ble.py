"""TB11: Verify BLE advertising is visible."""

import pytest

from hil_report import TbResult


@pytest.mark.asyncio
async def test_tb11_ble_discovery(report):
    """TB11: Verify BLE advertising is visible."""
    from bleak import BleakScanner

    devices = await BleakScanner.discover(timeout=10)
    bmu = [d for d in devices if d.name and "KXKM-BMU" in d.name]
    status = "PASS" if len(bmu) > 0 else "FAIL"
    report.add(TbResult("TB11", "BLE discovery", status, notes=f"found {len(bmu)} devices"))
    assert len(bmu) > 0, "No KXKM-BMU device found via BLE scan"
