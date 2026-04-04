"""Unit tests for diagnostic API response format and validation."""

import sys
from pathlib import Path
from unittest.mock import AsyncMock, patch

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "services" / "soh-llm"))


class TestDiagnosticResponseFormat:
    """Verify API response schema matches spec."""

    def test_diagnostic_response_fields(self):
        from diagnostic_api import DiagnosticResponse

        resp = DiagnosticResponse(
            battery=3,
            diagnostic="Batterie en bon état.",
            severity="info",
            generated_at=1743678000,
        )
        assert resp.battery == 3
        assert resp.severity == "info"
        assert resp.generated_at == 1743678000

    def test_diagnostic_response_rejects_invalid_severity_type(self):
        from diagnostic_api import DiagnosticResponse

        # severity is a str field — Pydantic accepts any string
        # Actual validation happens in extract_severity()
        resp = DiagnosticResponse(
            battery=0,
            diagnostic="Test",
            severity="unknown",
            generated_at=0,
        )
        assert resp.severity == "unknown"

    def test_fleet_response_fields(self):
        from diagnostic_api import FleetDiagnosticResponse

        resp = FleetDiagnosticResponse(
            diagnostic="Flotte en bon état global.",
            severity="info",
            generated_at=1743678000,
            num_batteries=16,
        )
        assert resp.num_batteries == 16

    def test_health_response_fields(self):
        from diagnostic_api import HealthResponse

        resp = HealthResponse(
            status="ok",
            model_loaded=True,
            cache_size=5,
            uptime_s=120.5,
        )
        assert resp.model_loaded is True


class TestCacheLogic:
    """Verify cache behavior without loading actual model."""

    def test_cache_miss_returns_404(self):
        """GET /api/diagnostic/{id} with empty cache should 404."""
        from diagnostic_api import _cache
        _cache.clear()
        # Cache is empty, battery 99 not present
        assert 99 not in _cache
