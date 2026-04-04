"""Shared fixtures for LLM diagnostic tests."""

import pytest


@pytest.fixture
def healthy_scores() -> dict:
    return {
        "battery": 3,
        "fleet_size": 12,
        "soh_score": 92.0,
        "rul_days": 280,
        "anomaly_score": 0.05,
        "r_ohmic_mohm": 14.2,
        "r_total_mohm": 20.1,
        "r_int_trend_mohm_per_day": 0.02,
        "v_avg_mv": 27200,
        "i_avg_a": 4.5,
        "cycle_count": 150,
        "fleet_health_pct": 91.0,
    }


@pytest.fixture
def critical_scores() -> dict:
    return {
        "battery": 7,
        "fleet_size": 16,
        "soh_score": 42.0,
        "rul_days": 12,
        "anomaly_score": 0.85,
        "r_ohmic_mohm": 68.5,
        "r_total_mohm": 102.3,
        "r_int_trend_mohm_per_day": 1.2,
        "v_avg_mv": 24100,
        "i_avg_a": 1.2,
        "cycle_count": 1800,
        "fleet_health_pct": 62.0,
    }


@pytest.fixture
def warning_scores() -> dict:
    return {
        "battery": 5,
        "fleet_size": 10,
        "soh_score": 73.0,
        "rul_days": 85,
        "anomaly_score": 0.35,
        "r_ohmic_mohm": 28.0,
        "r_total_mohm": 40.5,
        "r_int_trend_mohm_per_day": 0.12,
        "v_avg_mv": 26000,
        "i_avg_a": 3.0,
        "cycle_count": 600,
        "fleet_health_pct": 78.0,
    }
