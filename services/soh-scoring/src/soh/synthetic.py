"""Synthetic battery degradation data generator for training.

Generates realistic LiFePO4/Li-ion degradation profiles with four scenarios:
1. Calendar aging: R_int linear drift +0.5-2 mOhm/month
2. Cycle aging: R_int exponential rise after knee point
3. Sudden failure: step-change in R_int (connection degradation)
4. Temperature stress: accelerated R_int rise at high temperature

Each profile generates:
- Feature matrix: (seq_len, 17) matching ETL feature format
- Labels: soh_score (0-1), rul_days (int), anomaly_score (0-1)
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum

import numpy as np


class DegradationMode(Enum):
    CALENDAR = "calendar"
    CYCLE = "cycle"
    SUDDEN_FAILURE = "sudden_failure"
    TEMPERATURE_STRESS = "temperature_stress"
    HEALTHY = "healthy"


@dataclass
class SyntheticProfile:
    """A single synthetic battery degradation profile."""
    features: np.ndarray       # (seq_len, 17)
    soh_score: float           # 0.0-1.0
    rul_days: float            # estimated remaining useful life
    anomaly_score: float       # 0.0-1.0
    mode: DegradationMode
    battery_id: int = 0


def generate_profile(
    mode: DegradationMode,
    seq_len: int = 336,
    rng: np.random.Generator | None = None,
    severity: float | None = None,
) -> SyntheticProfile:
    """Generate a single synthetic degradation profile.

    Parameters
    ----------
    mode : DegradationMode
    seq_len : int — number of 30-min windows (336 = 7 days)
    rng : numpy random generator
    severity : float 0-1 — how degraded the battery is (None = random)
    """
    if rng is None:
        rng = np.random.default_rng()
    if severity is None:
        severity = rng.uniform(0.0, 1.0)

    features = np.zeros((seq_len, 17), dtype=np.float32)
    t = np.linspace(0, 1, seq_len)  # normalized time

    # Base healthy battery parameters (LiFePO4 24-30V)
    r_int_base = rng.uniform(10.0, 16.0)  # mOhm
    v_nominal = rng.uniform(25500, 27500)  # mV
    i_nominal = rng.uniform(1.5, 4.0)     # A

    if mode == DegradationMode.HEALTHY:
        r_int = r_int_base + rng.normal(0, 0.1, seq_len)
        soh = rng.uniform(0.90, 1.0)
        rul = rng.uniform(500, 2000)
        anomaly = rng.uniform(0.0, 0.05)

    elif mode == DegradationMode.CALENDAR:
        # Linear R_int drift: +0.5-2 mOhm/month -> over 7 days, small drift
        drift_rate = severity * 2.0  # mOhm/month
        monthly_fraction = 7.0 / 30.0
        r_int = r_int_base + drift_rate * monthly_fraction * t + rng.normal(0, 0.15, seq_len)
        soh = max(0.0, 1.0 - severity * 0.4)
        rul = max(10, (1.0 - severity) * 1500)
        anomaly = severity * 0.15

    elif mode == DegradationMode.CYCLE:
        # Exponential knee: R_int stable then accelerating rise
        knee = rng.uniform(0.3, 0.7)
        r_int = r_int_base + np.where(
            t < knee,
            severity * 0.5 * t,
            severity * 0.5 * knee + severity * 5.0 * (t - knee) ** 2,
        ) + rng.normal(0, 0.2, seq_len)
        soh = max(0.0, 1.0 - severity * 0.5)
        rul = max(5, (1.0 - severity) * 800)
        anomaly = severity * 0.3

    elif mode == DegradationMode.SUDDEN_FAILURE:
        # Step change in R_int at random point
        failure_point = int(rng.uniform(0.2, 0.8) * seq_len)
        r_int = np.full(seq_len, r_int_base, dtype=np.float32)
        step_magnitude = severity * 15.0  # up to 15 mOhm jump
        r_int[failure_point:] += step_magnitude
        r_int += rng.normal(0, 0.3, seq_len)
        soh = max(0.0, 1.0 - severity * 0.6)
        rul = max(1, (1.0 - severity) * 200)
        anomaly = min(1.0, severity * 0.8 + 0.2)

    elif mode == DegradationMode.TEMPERATURE_STRESS:
        # Accelerated degradation from temperature
        temp_factor = 1.0 + severity * 2.0
        r_int = r_int_base * (1.0 + 0.3 * severity * t * temp_factor) + rng.normal(0, 0.2, seq_len)
        soh = max(0.0, 1.0 - severity * 0.35)
        rul = max(20, (1.0 - severity) * 1000)
        anomaly = severity * 0.25

    else:
        raise ValueError(f"Unknown degradation mode: {mode}")

    # Fill feature columns
    features[:, 0] = r_int                              # r_int_mean (per window, ~= sample)
    features[:, 1] = np.abs(rng.normal(0.2, 0.05, seq_len))  # r_int_std
    features[:, 2] = np.gradient(r_int)                  # r_int_slope (approximate)
    features[:, 3] = r_int + rng.uniform(0.5, 2.0, seq_len)  # r_int_max
    features[:, 4] = r_int - rng.uniform(0.5, 2.0, seq_len)  # r_int_min

    # Voltage: degrades with SOH
    v_sag = (1.0 - soh) * 2000  # mV sag for degraded batteries
    features[:, 5] = v_nominal - v_sag + rng.normal(0, 100, seq_len)   # v_mean
    features[:, 6] = rng.uniform(30, 80, seq_len)                       # v_std
    features[:, 7] = features[:, 5] - rng.uniform(500, 2500, seq_len)   # v_min
    features[:, 8] = features[:, 5] + rng.uniform(200, 1500, seq_len)   # v_max
    features[:, 9] = features[:, 5] - rng.uniform(100, 800, seq_len)    # v_mean_under_load

    # Current
    features[:, 10] = i_nominal + rng.normal(0, 0.3, seq_len)           # i_mean
    features[:, 11] = rng.uniform(0.5, 2.0, seq_len)                    # i_std
    features[:, 12] = i_nominal * rng.uniform(2.0, 4.0, seq_len)        # i_peak
    features[:, 13] = rng.uniform(0.3, 0.8, seq_len)                    # i_duty_cycle

    # Energy
    features[:, 14] = np.cumsum(rng.uniform(0, 0.15, seq_len))          # ah_discharged
    features[:, 15] = np.cumsum(rng.uniform(0, 0.14, seq_len))          # ah_charged
    ce_base = 0.98 - severity * 0.15
    features[:, 16] = rng.normal(ce_base, 0.02, seq_len).clip(0.5, 1.1) # coulombic_efficiency

    return SyntheticProfile(
        features=features,
        soh_score=float(np.clip(soh, 0.0, 1.0)),
        rul_days=float(max(0, rul)),
        anomaly_score=float(np.clip(anomaly, 0.0, 1.0)),
        mode=mode,
    )


def generate_dataset(
    n_samples: int = 5000,
    seq_len: int = 336,
    seed: int = 42,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Generate full synthetic training dataset.

    Returns
    -------
    X : (n_samples, seq_len, 17) — feature matrices
    y_soh : (n_samples,) — SOH scores 0-1
    y_rul : (n_samples,) — RUL in days
    y_anomaly : (n_samples,) — anomaly scores 0-1
    """
    rng = np.random.default_rng(seed)

    modes = list(DegradationMode)
    # Distribution: 30% healthy, 25% calendar, 20% cycle, 15% sudden, 10% temp
    weights = [0.30, 0.25, 0.20, 0.15, 0.10]

    X = np.zeros((n_samples, seq_len, 17), dtype=np.float32)
    y_soh = np.zeros(n_samples, dtype=np.float32)
    y_rul = np.zeros(n_samples, dtype=np.float32)
    y_anomaly = np.zeros(n_samples, dtype=np.float32)

    for i in range(n_samples):
        mode = rng.choice(modes, p=weights)
        profile = generate_profile(mode, seq_len=seq_len, rng=rng)
        X[i] = profile.features
        y_soh[i] = profile.soh_score
        y_rul[i] = profile.rul_days
        y_anomaly[i] = profile.anomaly_score

    return X, y_soh, y_rul, y_anomaly
