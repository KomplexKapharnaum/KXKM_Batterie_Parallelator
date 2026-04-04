#!/usr/bin/env python3
"""
scenario_templates.py — Parametric battery context generators for 8 diagnostic scenarios.

Each scenario type produces a BatteryContext with realistic ranges for LiFePO4/Li-ion
24-30V batteries in the KXKM BMU fleet (2-23 batteries per unit).
"""

from __future__ import annotations

import random
from dataclasses import dataclass, field, asdict
from typing import Callable


@dataclass
class BatteryContext:
    """Battery context JSON structure for LLM prompt input."""
    battery_id: int
    fleet_size: int
    soh_pct: float              # 0-100
    rul_days: float             # estimated remaining useful life
    anomaly_score: float        # 0.0-1.0
    r_ohmic_mohm: float         # mOhm
    r_total_mohm: float         # mOhm
    r_int_trend_mohm_per_day: float  # mOhm/day over 7 days
    v_avg_mv: float             # mV
    i_avg_a: float              # A
    cycle_count: int
    fleet_health_pct: float     # 0-100
    soh_confidence: int         # 0-100
    chemistry: str              # "LiFePO4" or "Li-ion"
    scenario: str = ""          # scenario label (not included in prompt)

    def to_dict(self) -> dict:
        """Return dict for JSON serialization (excludes scenario label)."""
        d = asdict(self)
        d.pop("scenario", None)
        return d


# ---------------------------------------------------------------------------
# Parametric generators per scenario
# ---------------------------------------------------------------------------

def _healthy() -> BatteryContext:
    """Healthy battery, normal operation."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(85, 100), 1),
        rul_days=round(random.uniform(300, 800), 0),
        anomaly_score=round(random.uniform(0.0, 0.1), 2),
        r_ohmic_mohm=round(random.uniform(8.0, 18.0), 1),
        r_total_mohm=round(random.uniform(12.0, 25.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(-0.02, 0.05), 3),
        v_avg_mv=round(random.uniform(26000, 28500), 0),
        i_avg_a=round(random.uniform(0.5, 8.0), 1),
        cycle_count=random.randint(10, 300),
        fleet_health_pct=round(random.uniform(85, 98), 1),
        soh_confidence=random.randint(75, 100),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="healthy",
    )


def _early_degradation() -> BatteryContext:
    """Early degradation: R_int rising slowly, SOH 70-85%."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(70, 85), 1),
        rul_days=round(random.uniform(90, 300), 0),
        anomaly_score=round(random.uniform(0.1, 0.3), 2),
        r_ohmic_mohm=round(random.uniform(18.0, 30.0), 1),
        r_total_mohm=round(random.uniform(25.0, 42.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.05, 0.15), 3),
        v_avg_mv=round(random.uniform(25500, 27500), 0),
        i_avg_a=round(random.uniform(0.5, 8.0), 1),
        cycle_count=random.randint(300, 800),
        fleet_health_pct=round(random.uniform(75, 90), 1),
        soh_confidence=random.randint(60, 90),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="early_degradation",
    )


def _accelerated_degradation() -> BatteryContext:
    """Accelerated degradation: R_int knee point, SOH dropping fast."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(55, 75), 1),
        rul_days=round(random.uniform(20, 90), 0),
        anomaly_score=round(random.uniform(0.3, 0.6), 2),
        r_ohmic_mohm=round(random.uniform(28.0, 50.0), 1),
        r_total_mohm=round(random.uniform(40.0, 70.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.15, 0.5), 3),
        v_avg_mv=round(random.uniform(24500, 26500), 0),
        i_avg_a=round(random.uniform(0.5, 6.0), 1),
        cycle_count=random.randint(600, 1500),
        fleet_health_pct=round(random.uniform(65, 85), 1),
        soh_confidence=random.randint(50, 80),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="accelerated_degradation",
    )


def _connection_issue() -> BatteryContext:
    """Connection issue: sudden R_int jump, low confidence."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(60, 90), 1),
        rul_days=round(random.uniform(50, 200), 0),
        anomaly_score=round(random.uniform(0.5, 0.9), 2),
        r_ohmic_mohm=round(random.uniform(40.0, 120.0), 1),
        r_total_mohm=round(random.uniform(60.0, 180.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.5, 5.0), 3),
        v_avg_mv=round(random.uniform(25000, 28000), 0),
        i_avg_a=round(random.uniform(0.2, 5.0), 1),
        cycle_count=random.randint(50, 600),
        fleet_health_pct=round(random.uniform(70, 90), 1),
        soh_confidence=random.randint(10, 40),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="connection_issue",
    )


def _fleet_outlier() -> BatteryContext:
    """Fleet outlier: GNN detected anomaly relative to peers."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(6, 23),
        soh_pct=round(random.uniform(60, 80), 1),
        rul_days=round(random.uniform(40, 150), 0),
        anomaly_score=round(random.uniform(0.6, 0.95), 2),
        r_ohmic_mohm=round(random.uniform(25.0, 55.0), 1),
        r_total_mohm=round(random.uniform(35.0, 75.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.1, 0.4), 3),
        v_avg_mv=round(random.uniform(24500, 27000), 0),
        i_avg_a=round(random.uniform(0.5, 7.0), 1),
        cycle_count=random.randint(200, 1000),
        fleet_health_pct=round(random.uniform(80, 95), 1),
        soh_confidence=random.randint(55, 85),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="fleet_outlier",
    )


def _end_of_life() -> BatteryContext:
    """End of life: SOH < 60%, RUL < 30 days."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(30, 60), 1),
        rul_days=round(random.uniform(0, 30), 0),
        anomaly_score=round(random.uniform(0.7, 1.0), 2),
        r_ohmic_mohm=round(random.uniform(45.0, 100.0), 1),
        r_total_mohm=round(random.uniform(65.0, 150.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.3, 2.0), 3),
        v_avg_mv=round(random.uniform(23000, 25500), 0),
        i_avg_a=round(random.uniform(0.1, 3.0), 1),
        cycle_count=random.randint(1000, 3000),
        fleet_health_pct=round(random.uniform(55, 80), 1),
        soh_confidence=random.randint(40, 75),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="end_of_life",
    )


def _post_replacement() -> BatteryContext:
    """Post replacement: new battery, low cycle count, high SOH."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(4, 23),
        soh_pct=round(random.uniform(95, 100), 1),
        rul_days=round(random.uniform(600, 1200), 0),
        anomaly_score=round(random.uniform(0.0, 0.15), 2),
        r_ohmic_mohm=round(random.uniform(6.0, 12.0), 1),
        r_total_mohm=round(random.uniform(9.0, 18.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(-0.01, 0.02), 3),
        v_avg_mv=round(random.uniform(27000, 29000), 0),
        i_avg_a=round(random.uniform(0.5, 8.0), 1),
        cycle_count=random.randint(0, 20),
        fleet_health_pct=round(random.uniform(80, 95), 1),
        soh_confidence=random.randint(30, 60),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="post_replacement",
    )


def _fleet_imbalance() -> BatteryContext:
    """Fleet imbalance: one weak battery dragging others, low fleet health."""
    return BatteryContext(
        battery_id=random.randint(0, 22),
        fleet_size=random.randint(6, 23),
        soh_pct=round(random.uniform(50, 72), 1),
        rul_days=round(random.uniform(30, 120), 0),
        anomaly_score=round(random.uniform(0.4, 0.8), 2),
        r_ohmic_mohm=round(random.uniform(30.0, 65.0), 1),
        r_total_mohm=round(random.uniform(45.0, 90.0), 1),
        r_int_trend_mohm_per_day=round(random.uniform(0.1, 0.4), 3),
        v_avg_mv=round(random.uniform(24000, 26500), 0),
        i_avg_a=round(random.uniform(0.3, 5.0), 1),
        cycle_count=random.randint(400, 1200),
        fleet_health_pct=round(random.uniform(45, 70), 1),
        soh_confidence=random.randint(50, 80),
        chemistry=random.choice(["LiFePO4", "Li-ion"]),
        scenario="fleet_imbalance",
    )


# ---------------------------------------------------------------------------
# Registry
# ---------------------------------------------------------------------------

SCENARIO_GENERATORS: dict[str, Callable[[], BatteryContext]] = {
    "healthy": _healthy,
    "early_degradation": _early_degradation,
    "accelerated_degradation": _accelerated_degradation,
    "connection_issue": _connection_issue,
    "fleet_outlier": _fleet_outlier,
    "end_of_life": _end_of_life,
    "post_replacement": _post_replacement,
    "fleet_imbalance": _fleet_imbalance,
}


def generate_context_for_scenario(scenario: str) -> BatteryContext:
    """Generate a random battery context for a given scenario type."""
    if scenario not in SCENARIO_GENERATORS:
        raise ValueError(f"Unknown scenario: {scenario}. Valid: {list(SCENARIO_GENERATORS.keys())}")
    return SCENARIO_GENERATORS[scenario]()
