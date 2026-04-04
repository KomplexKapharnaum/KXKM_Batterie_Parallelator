"""Unit tests for severity extraction from diagnostic text."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "services" / "soh-llm"))

from prompt_template import extract_severity


class TestExplicitTags:
    def test_info_tag(self):
        assert extract_severity("Batterie en bon état. [INFO]") == "info"

    def test_warning_tag(self):
        assert extract_severity("Dégradation lente observée. [WARNING]") == "warning"

    def test_critical_tag(self):
        assert extract_severity("[CRITICAL] Remplacer la batterie.") == "critical"

    def test_tag_priority_critical_over_warning(self):
        assert extract_severity("[CRITICAL] Attention [WARNING]") == "critical"

    def test_tag_priority_warning_over_info(self):
        assert extract_severity("[WARNING] [INFO]") == "warning"

    def test_case_insensitive(self):
        assert extract_severity("OK. [info]") == "info"
        assert extract_severity("Bad. [Critical]") == "critical"


class TestKeywordFallback:
    def test_critical_keywords(self):
        texts = [
            "État critique, la batterie doit être remplacée immédiatement.",
            "Situation urgente, intervention requise.",
            "Batterie en fin de vie, hors service.",
        ]
        for t in texts:
            assert extract_severity(t) == "critical", f"Failed for: {t}"

    def test_warning_keywords(self):
        texts = [
            "Attention, la résistance interne augmente. Surveiller l'évolution.",
            "Dégradation progressive, planifier le remplacement.",
            "Vigilance requise sur la tendance R_int.",
        ]
        for t in texts:
            assert extract_severity(t) == "warning", f"Failed for: {t}"

    def test_default_info(self):
        assert extract_severity("Batterie en bon état, aucun problème détecté.") == "info"

    def test_empty_string(self):
        assert extract_severity("") == "info"
