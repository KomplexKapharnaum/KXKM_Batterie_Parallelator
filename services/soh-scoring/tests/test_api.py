"""Integration tests for SOH REST API."""

import pytest
from httpx import AsyncClient, ASGITransport

from soh.api import app


@pytest.fixture
def anyio_backend():
    return "asyncio"


@pytest.fixture
async def client():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as c:
        yield c


class TestHealthEndpoint:
    async def test_health(self, client):
        resp = await client.get("/health")
        assert resp.status_code == 200
        data = resp.json()
        assert data["status"] == "ok"


class TestBatteriesEndpoint:
    async def test_get_batteries(self, client):
        resp = await client.get("/api/soh/batteries")
        assert resp.status_code == 200
        data = resp.json()
        assert isinstance(data, list)

    async def test_get_battery_by_id(self, client):
        resp = await client.get("/api/soh/battery/0")
        # May return 404 if no data, or 200 with data
        assert resp.status_code in (200, 404)


class TestFleetEndpoint:
    async def test_get_fleet(self, client):
        resp = await client.get("/api/soh/fleet")
        assert resp.status_code in (200, 404)


class TestPredictEndpoint:
    async def test_predict(self, client):
        resp = await client.post("/api/soh/predict")
        # Should trigger inference (may return 200 or 503 if models not loaded)
        assert resp.status_code in (200, 503)


class TestHistoryEndpoint:
    async def test_history(self, client):
        resp = await client.get("/api/soh/history/0?days=7")
        assert resp.status_code in (200, 404)
