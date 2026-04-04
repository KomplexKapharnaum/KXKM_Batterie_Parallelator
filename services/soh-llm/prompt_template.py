"""Prompt construction and severity extraction for battery diagnostics."""

from __future__ import annotations

import re
from typing import Any


SYSTEM_MSG = "Tu es l'assistant diagnostic batterie du BMU KXKM."

SEVERITY_LEVELS = ("info", "warning", "critical")


def build_user_prompt(scores: dict[str, Any]) -> str:
    """Build the user prompt from ML scoring data.

    Args:
        scores: Dict with keys matching Phase 2 SOH API response format:
            battery, fleet_size, soh_score, rul_days, anomaly_score,
            r_ohmic_mohm, r_total_mohm, r_int_trend_mohm_per_day,
            v_avg_mv, i_avg_a, cycle_count, fleet_health_pct
    """
    return (
        f"Batterie {scores['battery']}, flotte de {scores.get('fleet_size', '?')} batteries.\n"
        f"SOH: {scores['soh_score']}%, RUL estimé: {scores['rul_days']} jours, "
        f"anomalie: {scores['anomaly_score']}.\n"
        f"R_int ohmique: {scores.get('r_ohmic_mohm', '?')} mΩ "
        f"(tendance: {scores.get('r_int_trend_mohm_per_day', '?')} mΩ/jour sur 7j).\n"
        f"R_int total: {scores.get('r_total_mohm', '?')} mΩ. "
        f"V moyen: {scores.get('v_avg_mv', '?')} mV, I moyen: {scores.get('i_avg_a', '?')} A.\n"
        f"Cycles: {scores.get('cycle_count', '?')}. "
        f"Santé flotte: {scores.get('fleet_health_pct', '?')}%."
    )


def build_messages(scores: dict[str, Any]) -> list[dict[str, str]]:
    """Build complete chat messages for the model."""
    return [
        {"role": "system", "content": SYSTEM_MSG},
        {"role": "user", "content": build_user_prompt(scores)},
    ]


def extract_severity(text: str) -> str:
    """Extract severity level from generated diagnostic text.

    Looks for explicit [INFO]/[WARNING]/[CRITICAL] tags first,
    then falls back to keyword-based heuristic.
    """
    upper = text.upper()
    for tag in ["CRITICAL", "WARNING", "INFO"]:
        if f"[{tag}]" in upper:
            return tag.lower()

    lower = text.lower()
    critical_kw = ["critique", "urgent", "immédiat", "remplacer", "hors service", "fin de vie"]
    warning_kw = ["attention", "surveiller", "dégradation", "vigilance", "planifier", "prévoir"]

    if any(kw in lower for kw in critical_kw):
        return "critical"
    if any(kw in lower for kw in warning_kw):
        return "warning"
    return "info"


def clean_diagnostic(text: str) -> str:
    """Clean generated diagnostic text: remove severity tags, normalize whitespace."""
    # Remove severity tags from display text
    text = re.sub(r"\s*\[(INFO|WARNING|CRITICAL)\]\s*", "", text, flags=re.IGNORECASE)
    # Normalize whitespace
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip()
