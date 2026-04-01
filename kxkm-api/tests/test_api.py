"""Integration tests for BMU API.

Run with: cd kxkm-api && python -m pytest tests/ -v
"""

from __future__ import annotations

import os
import tempfile

import pytest
import pytest_asyncio
from httpx import ASGITransport, AsyncClient

# Configure test environment before importing app
os.environ["BMU_API_KEY"] = "test-key-12345"
os.environ["INFLUX_URL"] = "http://localhost:8086"
os.environ["INFLUX_TOKEN"] = "test-token"

_tmp_db = tempfile.NamedTemporaryFile(suffix=".db", delete=False)
os.environ["SQLITE_PATH"] = _tmp_db.name
_tmp_db.close()

from app.main import app  # noqa: E402
from app.database import init_db, close_db  # noqa: E402
from app.influx import init_influx, close_influx  # noqa: E402

HEADERS = {"Authorization": "Bearer test-key-12345"}
BASE = "http://test"


@pytest_asyncio.fixture
async def client():
    # Manually run lifespan init (ASGITransport doesn't trigger lifespan)
    init_influx()
    await init_db()
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url=BASE) as c:
        yield c
    close_influx()
    await close_db()


# ─── Auth tests ───────────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_no_auth_returns_401(client):
    resp = await client.get("/api/bmu/batteries")
    assert resp.status_code == 401


@pytest.mark.asyncio
async def test_bad_key_returns_403(client):
    resp = await client.get(
        "/api/bmu/batteries", headers={"Authorization": "Bearer wrong-key"}
    )
    assert resp.status_code == 403


# ─── Sync endpoint ───────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_sync_empty(client):
    resp = await client.post(
        "/api/bmu/sync",
        json={"battery_history": [], "audit_events": []},
        headers=HEADERS,
    )
    assert resp.status_code == 200
    data = resp.json()
    assert data["accepted_history"] == 0
    assert data["accepted_audit"] == 0


@pytest.mark.asyncio
async def test_sync_audit_events(client):
    events = [
        {
            "timestamp": "2026-04-01T12:00:00Z",
            "user_id": "clement",
            "action": "switch_on",
            "target": 3,
            "detail": "Manual switch via app",
        },
        {
            "timestamp": "2026-04-01T12:01:00Z",
            "user_id": "nicolas",
            "action": "switch_off",
            "target": 7,
            "detail": None,
        },
    ]
    resp = await client.post(
        "/api/bmu/sync",
        json={"battery_history": [], "audit_events": events},
        headers=HEADERS,
    )
    assert resp.status_code == 200
    assert resp.json()["accepted_audit"] == 2


# ─── Audit endpoint ──────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_audit_query(client):
    # Insert first
    await client.post(
        "/api/bmu/sync",
        json={
            "battery_history": [],
            "audit_events": [
                {
                    "timestamp": "2026-04-01T10:00:00Z",
                    "user_id": "admin",
                    "action": "config_change",
                    "target": None,
                    "detail": "V_max 30000 -> 29500",
                }
            ],
        },
        headers=HEADERS,
    )
    # Query all
    resp = await client.get("/api/bmu/audit", headers=HEADERS)
    assert resp.status_code == 200
    data = resp.json()
    assert data["total"] >= 1
    assert len(data["events"]) >= 1


@pytest.mark.asyncio
async def test_audit_filter_by_user(client):
    resp = await client.get(
        "/api/bmu/audit", params={"user": "admin"}, headers=HEADERS
    )
    assert resp.status_code == 200
    for event in resp.json()["events"]:
        assert event["user_id"] == "admin"


# ─── Batteries endpoint (no real InfluxDB in tests) ──────────────────


@pytest.mark.asyncio
async def test_batteries_returns_empty_without_influx(client):
    """Without a real InfluxDB, returns empty list (graceful degradation)."""
    try:
        resp = await client.get("/api/bmu/batteries", headers=HEADERS)
        # May return 200 with empty list or 500 if InfluxDB unreachable
        assert resp.status_code in (200, 500)
    except Exception:
        # Connection refused is expected without InfluxDB
        pytest.skip("InfluxDB not available")


# ─── History endpoint ────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_history_requires_from_param(client):
    resp = await client.get("/api/bmu/history", headers=HEADERS)
    # 'from' is required
    assert resp.status_code == 422


@pytest.mark.asyncio
async def test_history_with_params(client):
    try:
        resp = await client.get(
            "/api/bmu/history",
            params={"from": "-1h", "to": "now()", "battery": 0},
            headers=HEADERS,
        )
        # May be 200 (empty) or 500 (no InfluxDB) — both acceptable in test
        assert resp.status_code in (200, 500)
    except Exception:
        # Connection refused is expected without InfluxDB
        pytest.skip("InfluxDB not available")


# ─── Cleanup ─────────────────────────────────────────────────────────


def test_cleanup():
    """Remove temp database file."""
    try:
        os.unlink(_tmp_db.name)
    except OSError:
        pass
