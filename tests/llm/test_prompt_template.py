"""Unit tests for prompt construction and text processing."""

import sys
from pathlib import Path

# Add service module to path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "services" / "soh-llm"))

from prompt_template import (
    SYSTEM_MSG,
    build_messages,
    build_user_prompt,
    clean_diagnostic,
    extract_severity,
)


class TestBuildUserPrompt:
    def test_contains_battery_id(self, healthy_scores):
        prompt = build_user_prompt(healthy_scores)
        assert "Batterie 3" in prompt

    def test_contains_soh(self, healthy_scores):
        prompt = build_user_prompt(healthy_scores)
        assert "92.0%" in prompt

    def test_contains_rint_values(self, healthy_scores):
        prompt = build_user_prompt(healthy_scores)
        assert "14.2 mΩ" in prompt
        assert "20.1 mΩ" in prompt

    def test_contains_trend(self, healthy_scores):
        prompt = build_user_prompt(healthy_scores)
        assert "0.02 mΩ/jour" in prompt

    def test_contains_fleet_info(self, healthy_scores):
        prompt = build_user_prompt(healthy_scores)
        assert "flotte de 12 batteries" in prompt
        assert "91.0%" in prompt

    def test_missing_fields_use_placeholder(self):
        minimal = {"battery": 1, "soh_score": 80, "rul_days": 100, "anomaly_score": 0.1}
        prompt = build_user_prompt(minimal)
        assert "Batterie 1" in prompt
        assert "?" in prompt  # Missing fields show ?


class TestBuildMessages:
    def test_returns_system_and_user(self, healthy_scores):
        messages = build_messages(healthy_scores)
        assert len(messages) == 2
        assert messages[0]["role"] == "system"
        assert messages[0]["content"] == SYSTEM_MSG
        assert messages[1]["role"] == "user"

    def test_system_msg_is_french(self):
        assert "batterie" in SYSTEM_MSG.lower()
        assert "KXKM" in SYSTEM_MSG


class TestCleanDiagnostic:
    def test_removes_info_tag(self):
        assert "[INFO]" not in clean_diagnostic("Batterie OK. [INFO]")

    def test_removes_warning_tag(self):
        assert "[WARNING]" not in clean_diagnostic("Dégradation. [WARNING]")

    def test_removes_critical_tag(self):
        assert "[CRITICAL]" not in clean_diagnostic("[CRITICAL] Remplacer.")

    def test_case_insensitive_tag_removal(self):
        assert "[info]" not in clean_diagnostic("OK. [info]").lower()

    def test_preserves_content(self):
        result = clean_diagnostic("Batterie en bon état. [INFO]")
        assert "Batterie en bon état." in result

    def test_normalizes_excess_newlines(self):
        result = clean_diagnostic("Ligne 1.\n\n\n\nLigne 2.")
        assert "\n\n\n" not in result
