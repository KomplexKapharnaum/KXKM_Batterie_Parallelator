"""TB04-TB10: Semi-automatic tests requiring operator interaction."""

from hil_report import TbResult


def _run_manual(tb_id, title, instruction, hil_device, report):
    """Run a manual/semi-auto test bench step."""
    print(f"\n{'=' * 60}")
    print(f"  {tb_id}: {title}")
    print(f"{'=' * 60}")
    print(f"  Instructions operateur:")
    print(f"    1. {instruction}")
    print(f"    2. Observer le comportement du device")
    input("  Appuyer ENTREE quand pret...")
    input("  Appuyer ENTREE apres observation...")
    hil_device.capture_for(10)
    resp = input(f"  {tb_id} resultat (p=PASS / f=FAIL): ").strip().lower()
    status = "PASS" if resp.startswith("p") else "FAIL"
    notes = input("  Notes (optionnel): ").strip()
    report.add(TbResult(tb_id, title, status, notes=notes))
    assert status == "PASS", f"{tb_id} marked as FAIL by operator"


def test_tb04_manual(hil_device, report):
    """TB04: I2C SDA short recovery."""
    _run_manual(
        "TB04", "I2C SDA short recovery",
        "Court-circuiter SDA 2s puis relacher",
        hil_device, report,
    )


def test_tb05_manual(hil_device, report):
    """TB05: Battery disconnect live."""
    _run_manual(
        "TB05", "Battery disconnect live",
        "Debrancher batterie i pendant fonctionnement",
        hil_device, report,
    )


def test_tb06_manual(hil_device, report):
    """TB06: Over-voltage injection."""
    _run_manual(
        "TB06", "Over-voltage injection",
        "PSU sur-tension 32V sur rail",
        hil_device, report,
    )


def test_tb07_manual(hil_device, report):
    """TB07: Over-current load dump."""
    _run_manual(
        "TB07", "Over-current load dump",
        "Appliquer charge 100A sur sortie",
        hil_device, report,
    )


def test_tb08_manual(hil_device, report):
    """TB08: Over-temperature forced."""
    _run_manual(
        "TB08", "Over-temperature forced",
        "Heat gun sur INA237 pendant 30s",
        hil_device, report,
    )


def test_tb09_manual(hil_device, report):
    """TB09: Output short-circuit."""
    _run_manual(
        "TB09", "Output short-circuit",
        "Court-circuiter sortie puissance",
        hil_device, report,
    )


def test_tb10_manual(hil_device, report):
    """TB10: MCU power cut recovery."""
    _run_manual(
        "TB10", "MCU power cut recovery",
        "Couper alim MCU 5s puis rebrancher",
        hil_device, report,
    )
