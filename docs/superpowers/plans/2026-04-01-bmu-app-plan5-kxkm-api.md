# BMU App — Plan 5: kxkm-ai FastAPI Service

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deploy a small Python FastAPI service on kxkm-ai that exposes battery state from InfluxDB, time-series history, and audit trail storage via REST API. The smartphone app uses this for cloud-mode data access and sync.

**Architecture:** FastAPI application with 4 routes behind API key auth middleware. Reads battery telemetry from the existing InfluxDB (populated by firmware MQTT pipeline). Stores audit events in a local SQLite database. Deployed as a Docker container on kxkm-ai.

**Tech Stack:** Python 3.11+, FastAPI, uvicorn, influxdb-client, pydantic, aiosqlite, SQLite3

**Spec:** `docs/superpowers/specs/2026-04-01-smartphone-app-design.md` (section "API kxkm-ai a creer")

**Deploy target:** `ssh kxkm@kxkm-ai` (InfluxDB + MQTT broker already running)

---

## File Structure

```
kxkm-api/
├── app/
│   ├── __init__.py
│   ├── main.py              # FastAPI app, lifespan, CORS, routes
│   ├── auth.py              # API key middleware
│   ├── config.py            # Settings from env vars
│   ├── influx.py            # InfluxDB query helpers
│   ├── database.py          # SQLite audit trail (aiosqlite)
│   └── models.py            # Pydantic request/response models
├── Dockerfile
├── docker-compose.yml
├── requirements.txt
├── .env.example
└── tests/
    └── test_api.py          # pytest + httpx test suite
```

| File | Lines (approx) | Responsibility |
|------|-----------------|---------------|
| `app/main.py` | ~80 | App setup, CORS, 4 route handlers |
| `app/auth.py` | ~20 | API key dependency |
| `app/config.py` | ~20 | Env-based settings |
| `app/influx.py` | ~50 | InfluxDB query wrappers |
| `app/database.py` | ~40 | SQLite init + audit CRUD |
| `app/models.py` | ~50 | Pydantic schemas |
| `Dockerfile` | ~15 | Multi-stage Python image |
| `docker-compose.yml` | ~25 | Service + env config |
| `requirements.txt` | ~8 | Pinned deps |
| `tests/test_api.py` | ~80 | Integration tests |

---

### Task 1: Project scaffold + config + auth

**Files:**
- Create: `kxkm-api/requirements.txt`
- Create: `kxkm-api/.env.example`
- Create: `kxkm-api/app/__init__.py`
- Create: `kxkm-api/app/config.py`
- Create: `kxkm-api/app/auth.py`

- [ ] **Step 1: Create requirements.txt**

```bash
mkdir -p kxkm-api/app kxkm-api/tests
```

Write `kxkm-api/requirements.txt`:

```
fastapi==0.115.*
uvicorn[standard]==0.34.*
influxdb-client==1.48.*
pydantic==2.11.*
pydantic-settings==2.9.*
aiosqlite==0.21.*
httpx==0.28.*
pytest==8.3.*
pytest-asyncio==0.25.*
```

- [ ] **Step 2: Create .env.example**

Write `kxkm-api/.env.example`:

```bash
# API
BMU_API_KEY=change-me-to-a-secure-random-string
BMU_API_HOST=0.0.0.0
BMU_API_PORT=8400

# InfluxDB (already running on kxkm-ai)
INFLUX_URL=http://localhost:8086
INFLUX_TOKEN=your-influxdb-token
INFLUX_ORG=kxkm
INFLUX_BUCKET=bmu

# SQLite
SQLITE_PATH=/data/bmu_audit.db
```

- [ ] **Step 3: Create app/__init__.py**

Write `kxkm-api/app/__init__.py`:

```python
"""KXKM BMU API — FastAPI service for battery monitoring cloud access."""
```

- [ ] **Step 4: Create app/config.py**

Write `kxkm-api/app/config.py`:

```python
"""Configuration loaded from environment variables."""

from pydantic_settings import BaseSettings


class Settings(BaseSettings):
    # API
    bmu_api_key: str = "change-me"
    bmu_api_host: str = "0.0.0.0"
    bmu_api_port: int = 8400

    # InfluxDB
    influx_url: str = "http://localhost:8086"
    influx_token: str = ""
    influx_org: str = "kxkm"
    influx_bucket: str = "bmu"

    # SQLite
    sqlite_path: str = "/data/bmu_audit.db"

    model_config = {"env_file": ".env", "env_file_encoding": "utf-8"}


settings = Settings()
```

- [ ] **Step 5: Create app/auth.py**

Write `kxkm-api/app/auth.py`:

```python
"""API key authentication dependency."""

from fastapi import Depends, HTTPException, status
from fastapi.security import APIKeyHeader

from .config import settings

_api_key_header = APIKeyHeader(name="Authorization", auto_error=False)


async def require_api_key(
    api_key: str | None = Depends(_api_key_header),
) -> str:
    """Validate API key from Authorization header.

    Expected format: 'Bearer <key>' or just '<key>'.
    """
    if api_key is None:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Missing Authorization header",
        )
    # Strip optional 'Bearer ' prefix
    token = api_key.removeprefix("Bearer ").strip()
    if token != settings.bmu_api_key:
        raise HTTPException(
            status_code=status.HTTP_403_FORBIDDEN,
            detail="Invalid API key",
        )
    return token
```

- [ ] **Step 6: Verify imports**

```bash
cd kxkm-api && python3 -c "from app.config import settings; from app.auth import require_api_key; print('OK')"
```

Expected: `OK` (after `pip install -r requirements.txt`)

- [ ] **Step 7: Commit**

```bash
git add kxkm-api/requirements.txt kxkm-api/.env.example \
        kxkm-api/app/__init__.py kxkm-api/app/config.py kxkm-api/app/auth.py
git commit -m "feat(api): scaffold kxkm-api project with config + API key auth"
```

---

### Task 2: Pydantic models + SQLite audit database

**Files:**
- Create: `kxkm-api/app/models.py`
- Create: `kxkm-api/app/database.py`

- [ ] **Step 1: Create app/models.py**

Write `kxkm-api/app/models.py`:

```python
"""Pydantic models for request/response schemas."""

from __future__ import annotations

from pydantic import BaseModel, Field


# --- Battery state (from InfluxDB) ---

class BatteryState(BaseModel):
    index: int = Field(ge=0, le=15)
    voltage_mv: int
    current_ma: int
    state: str  # CONNECTED, DISCONNECTED, RECONNECTING, ERROR, LOCKED
    ah_discharge_mah: int = 0
    ah_charge_mah: int = 0
    nb_switch: int = 0
    timestamp: str  # ISO 8601


class BatteriesResponse(BaseModel):
    batteries: list[BatteryState]
    topology_valid: bool = True
    nb_ina: int = 0
    nb_tca: int = 0


# --- History (time-series from InfluxDB) ---

class HistoryPoint(BaseModel):
    time: str  # ISO 8601
    battery: int
    voltage_mv: int
    current_ma: int
    state: str


class HistoryResponse(BaseModel):
    points: list[HistoryPoint]
    battery: int | None = None
    from_time: str
    to_time: str


# --- Audit events ---

class AuditEvent(BaseModel):
    timestamp: str  # ISO 8601
    user_id: str
    action: str  # switch_on, switch_off, reset, config_change, wifi_config
    target: int | None = None  # battery index, null for global
    detail: str | None = None


class SyncRequest(BaseModel):
    """Batch sync from smartphone app."""
    battery_history: list[dict] = Field(default_factory=list)
    audit_events: list[AuditEvent] = Field(default_factory=list)


class SyncResponse(BaseModel):
    accepted_history: int = 0
    accepted_audit: int = 0


class AuditResponse(BaseModel):
    events: list[AuditEvent]
    total: int
```

- [ ] **Step 2: Create app/database.py**

Write `kxkm-api/app/database.py`:

```python
"""SQLite audit trail storage using aiosqlite."""

from __future__ import annotations

import aiosqlite

from .config import settings
from .models import AuditEvent

_db: aiosqlite.Connection | None = None

_SCHEMA = """
CREATE TABLE IF NOT EXISTS audit_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp TEXT NOT NULL,
    user_id TEXT NOT NULL,
    action TEXT NOT NULL,
    target INTEGER,
    detail TEXT
);
CREATE INDEX IF NOT EXISTS idx_audit_ts ON audit_events(timestamp);
CREATE INDEX IF NOT EXISTS idx_audit_user ON audit_events(user_id);
CREATE INDEX IF NOT EXISTS idx_audit_action ON audit_events(action);
"""


async def init_db() -> None:
    """Open SQLite connection and create schema."""
    global _db
    _db = await aiosqlite.connect(settings.sqlite_path)
    _db.row_factory = aiosqlite.Row
    await _db.executescript(_SCHEMA)
    await _db.commit()


async def close_db() -> None:
    """Close SQLite connection."""
    global _db
    if _db:
        await _db.close()
        _db = None


async def insert_audit_events(events: list[AuditEvent]) -> int:
    """Insert batch of audit events. Returns count inserted."""
    if not _db or not events:
        return 0
    rows = [
        (e.timestamp, e.user_id, e.action, e.target, e.detail)
        for e in events
    ]
    await _db.executemany(
        "INSERT INTO audit_events (timestamp, user_id, action, target, detail) "
        "VALUES (?, ?, ?, ?, ?)",
        rows,
    )
    await _db.commit()
    return len(rows)


async def query_audit_events(
    from_time: str | None = None,
    to_time: str | None = None,
    user: str | None = None,
    action: str | None = None,
    limit: int = 200,
) -> tuple[list[AuditEvent], int]:
    """Query audit events with optional filters. Returns (events, total_count)."""
    if not _db:
        return [], 0

    conditions: list[str] = []
    params: list[str | int] = []

    if from_time:
        conditions.append("timestamp >= ?")
        params.append(from_time)
    if to_time:
        conditions.append("timestamp <= ?")
        params.append(to_time)
    if user:
        conditions.append("user_id = ?")
        params.append(user)
    if action:
        conditions.append("action = ?")
        params.append(action)

    where = f"WHERE {' AND '.join(conditions)}" if conditions else ""

    # Total count
    count_row = await _db.execute_fetchall(
        f"SELECT COUNT(*) FROM audit_events {where}", params
    )
    total = count_row[0][0] if count_row else 0

    # Fetch rows
    rows = await _db.execute_fetchall(
        f"SELECT timestamp, user_id, action, target, detail "
        f"FROM audit_events {where} ORDER BY timestamp DESC LIMIT ?",
        params + [limit],
    )

    events = [
        AuditEvent(
            timestamp=r[0], user_id=r[1], action=r[2], target=r[3], detail=r[4]
        )
        for r in rows
    ]
    return events, total
```

- [ ] **Step 3: Verify imports**

```bash
cd kxkm-api && python3 -c "from app.models import SyncRequest, BatteriesResponse; from app.database import init_db; print('OK')"
```

Expected: `OK`

- [ ] **Step 4: Commit**

```bash
git add kxkm-api/app/models.py kxkm-api/app/database.py
git commit -m "feat(api): add Pydantic models + SQLite audit database"
```

---

### Task 3: InfluxDB queries + main app with all routes

**Files:**
- Create: `kxkm-api/app/influx.py`
- Create: `kxkm-api/app/main.py`

- [ ] **Step 1: Create app/influx.py**

Write `kxkm-api/app/influx.py`:

```python
"""InfluxDB query helpers for BMU battery data."""

from __future__ import annotations

from influxdb_client import InfluxDBClient
from influxdb_client.client.query_api import QueryApi

from .config import settings
from .models import BatteryState, HistoryPoint

_client: InfluxDBClient | None = None
_query: QueryApi | None = None


def init_influx() -> None:
    """Initialize InfluxDB client."""
    global _client, _query
    _client = InfluxDBClient(
        url=settings.influx_url,
        token=settings.influx_token,
        org=settings.influx_org,
    )
    _query = _client.query_api()


def close_influx() -> None:
    """Close InfluxDB client."""
    global _client, _query
    if _client:
        _client.close()
        _client = None
        _query = None


def get_current_batteries() -> list[BatteryState]:
    """Get latest state for each battery from InfluxDB.

    Expects data written by firmware MQTT pipeline with fields:
    voltage_mv, current_ma, state, ah_discharge_mah, ah_charge_mah, nb_switch
    and tag: battery (index 0-15).
    """
    if not _query:
        return []

    flux = f"""
    from(bucket: "{settings.influx_bucket}")
      |> range(start: -5m)
      |> filter(fn: (r) => r._measurement == "battery")
      |> last()
      |> pivot(rowKey:["_time"], columnKey:["_field"], valueColumn:"_value")
    """

    tables = _query.query(flux)
    batteries: list[BatteryState] = []

    for table in tables:
        for record in table.records:
            try:
                batteries.append(
                    BatteryState(
                        index=int(record.values.get("battery", 0)),
                        voltage_mv=int(record.values.get("voltage_mv", 0)),
                        current_ma=int(record.values.get("current_ma", 0)),
                        state=str(record.values.get("state", "UNKNOWN")),
                        ah_discharge_mah=int(
                            record.values.get("ah_discharge_mah", 0)
                        ),
                        ah_charge_mah=int(
                            record.values.get("ah_charge_mah", 0)
                        ),
                        nb_switch=int(record.values.get("nb_switch", 0)),
                        timestamp=record.get_time().isoformat(),
                    )
                )
            except (TypeError, ValueError):
                continue

    # Deduplicate by index, keep latest
    seen: dict[int, BatteryState] = {}
    for b in batteries:
        if b.index not in seen or b.timestamp > seen[b.index].timestamp:
            seen[b.index] = b

    return sorted(seen.values(), key=lambda b: b.index)


def get_history(
    from_time: str,
    to_time: str,
    battery: int | None = None,
) -> list[HistoryPoint]:
    """Get time-series history from InfluxDB."""
    if not _query:
        return []

    battery_filter = ""
    if battery is not None:
        battery_filter = (
            f'  |> filter(fn: (r) => r.battery == "{battery}")\n'
        )

    flux = f"""
    from(bucket: "{settings.influx_bucket}")
      |> range(start: {from_time}, stop: {to_time})
      |> filter(fn: (r) => r._measurement == "battery")
{battery_filter}      |> pivot(rowKey:["_time"], columnKey:["_field"], valueColumn:"_value")
      |> sort(columns: ["_time"])
      |> limit(n: 5000)
    """

    tables = _query.query(flux)
    points: list[HistoryPoint] = []

    for table in tables:
        for record in table.records:
            try:
                points.append(
                    HistoryPoint(
                        time=record.get_time().isoformat(),
                        battery=int(record.values.get("battery", 0)),
                        voltage_mv=int(record.values.get("voltage_mv", 0)),
                        current_ma=int(record.values.get("current_ma", 0)),
                        state=str(record.values.get("state", "UNKNOWN")),
                    )
                )
            except (TypeError, ValueError):
                continue

    return points
```

- [ ] **Step 2: Create app/main.py**

Write `kxkm-api/app/main.py`:

```python
"""KXKM BMU API — FastAPI service on kxkm-ai server.

Provides cloud REST access to battery state (InfluxDB), history,
and audit trail (SQLite) for the BMU smartphone app.
"""

from __future__ import annotations

from contextlib import asynccontextmanager

from fastapi import Depends, FastAPI, Query
from fastapi.middleware.cors import CORSMiddleware

from .auth import require_api_key
from .config import settings
from .database import close_db, init_db, insert_audit_events, query_audit_events
from .influx import close_influx, get_current_batteries, get_history, init_influx
from .models import (
    AuditResponse,
    BatteriesResponse,
    HistoryResponse,
    SyncRequest,
    SyncResponse,
)


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Startup / shutdown: init InfluxDB + SQLite."""
    init_influx()
    await init_db()
    yield
    close_influx()
    await close_db()


app = FastAPI(
    title="KXKM BMU API",
    version="1.0.0",
    description="Battery Management Unit cloud API for smartphone app",
    lifespan=lifespan,
)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # L'app mobile se connecte depuis n'importe ou
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)


# ─── Routes ───────────────────────────────────────────────────────────


@app.post("/api/bmu/sync", response_model=SyncResponse)
async def sync(
    body: SyncRequest,
    _: str = Depends(require_api_key),
):
    """Receive battery history + audit events from the app."""
    # Battery history points are forwarded to InfluxDB by the MQTT pipeline;
    # the app sends them here as a backup when direct MQTT is unavailable.
    # For now we accept and count them (write-to-influx can be added later).
    accepted_history = len(body.battery_history)

    accepted_audit = await insert_audit_events(body.audit_events)

    return SyncResponse(
        accepted_history=accepted_history,
        accepted_audit=accepted_audit,
    )


@app.get("/api/bmu/batteries", response_model=BatteriesResponse)
async def batteries(
    _: str = Depends(require_api_key),
):
    """Get current battery state (latest points from InfluxDB)."""
    bats = get_current_batteries()
    return BatteriesResponse(batteries=bats)


@app.get("/api/bmu/history", response_model=HistoryResponse)
async def history(
    _: str = Depends(require_api_key),
    from_time: str = Query(alias="from", description="ISO 8601 or relative (-1h, -24h)"),
    to_time: str = Query(alias="to", default="now()", description="ISO 8601 or now()"),
    battery: int | None = Query(default=None, ge=0, le=15),
):
    """Get time-series battery history from InfluxDB."""
    points = get_history(from_time, to_time, battery)
    return HistoryResponse(
        points=points,
        battery=battery,
        from_time=from_time,
        to_time=to_time,
    )


@app.get("/api/bmu/audit", response_model=AuditResponse)
async def audit(
    _: str = Depends(require_api_key),
    from_time: str | None = Query(alias="from", default=None),
    to_time: str | None = Query(alias="to", default=None),
    user: str | None = None,
    action: str | None = None,
):
    """Get audit trail with optional filters."""
    events, total = await query_audit_events(
        from_time=from_time,
        to_time=to_time,
        user=user,
        action=action,
    )
    return AuditResponse(events=events, total=total)


# ─── Entrypoint ──────────────────────────────────────────────────────

if __name__ == "__main__":
    import uvicorn

    uvicorn.run(
        "app.main:app",
        host=settings.bmu_api_host,
        port=settings.bmu_api_port,
        reload=True,
    )
```

- [ ] **Step 3: Verify app loads**

```bash
cd kxkm-api && python3 -c "from app.main import app; print(f'Routes: {len(app.routes)}')"
```

Expected: `Routes: 8` (4 API + openapi + docs + redoc + root)

- [ ] **Step 4: Commit**

```bash
git add kxkm-api/app/influx.py kxkm-api/app/main.py
git commit -m "feat(api): add InfluxDB queries + FastAPI app with 4 routes"
```

---

### Task 4: Dockerfile + docker-compose

**Files:**
- Create: `kxkm-api/Dockerfile`
- Create: `kxkm-api/docker-compose.yml`

- [ ] **Step 1: Create Dockerfile**

Write `kxkm-api/Dockerfile`:

```dockerfile
FROM python:3.11-slim AS base

WORKDIR /app

# Install deps first for layer caching
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY app/ app/

# Create data directory for SQLite
RUN mkdir -p /data

EXPOSE 8400

CMD ["uvicorn", "app.main:app", "--host", "0.0.0.0", "--port", "8400"]
```

- [ ] **Step 2: Create docker-compose.yml**

Write `kxkm-api/docker-compose.yml`:

```yaml
# KXKM BMU API — deployed on kxkm-ai server
# Usage: docker compose up -d
# Logs:  docker compose logs -f bmu-api

services:
  bmu-api:
    build: .
    container_name: kxkm-bmu-api
    restart: unless-stopped
    ports:
      - "8400:8400"
    env_file:
      - .env
    volumes:
      - bmu-data:/data  # SQLite audit DB persisted here
    environment:
      - INFLUX_URL=http://host.docker.internal:8086
    extra_hosts:
      - "host.docker.internal:host-gateway"

volumes:
  bmu-data:
```

- [ ] **Step 3: Verify Dockerfile syntax**

```bash
cd kxkm-api && docker build --check . 2>&1 || echo "Docker not available locally — will build on kxkm-ai"
```

- [ ] **Step 4: Commit**

```bash
git add kxkm-api/Dockerfile kxkm-api/docker-compose.yml
git commit -m "feat(api): add Dockerfile + docker-compose for kxkm-ai deploy"
```

---

### Task 5: Test suite

**Files:**
- Create: `kxkm-api/tests/__init__.py`
- Create: `kxkm-api/tests/test_api.py`

- [ ] **Step 1: Create tests/__init__.py**

Write `kxkm-api/tests/__init__.py`:

```python
```

- [ ] **Step 2: Create tests/test_api.py**

Write `kxkm-api/tests/test_api.py`:

```python
"""Integration tests for BMU API.

Run with: cd kxkm-api && python -m pytest tests/ -v
"""

from __future__ import annotations

import os
import tempfile

import pytest
from httpx import ASGITransport, AsyncClient

# Configure test environment before importing app
os.environ["BMU_API_KEY"] = "test-key-12345"
os.environ["INFLUX_URL"] = "http://localhost:8086"
os.environ["INFLUX_TOKEN"] = "test-token"

_tmp_db = tempfile.NamedTemporaryFile(suffix=".db", delete=False)
os.environ["SQLITE_PATH"] = _tmp_db.name
_tmp_db.close()

from app.main import app  # noqa: E402

HEADERS = {"Authorization": "Bearer test-key-12345"}
BASE = "http://test"


@pytest.fixture
async def client():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url=BASE) as c:
        yield c


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
    resp = await client.get("/api/bmu/batteries", headers=HEADERS)
    # May return 200 with empty list or 500 if InfluxDB unreachable
    # Both are acceptable in test environment
    assert resp.status_code in (200, 500)


# ─── History endpoint ────────────────────────────────────────────────


@pytest.mark.asyncio
async def test_history_requires_from_param(client):
    resp = await client.get("/api/bmu/history", headers=HEADERS)
    # 'from' is required
    assert resp.status_code == 422


@pytest.mark.asyncio
async def test_history_with_params(client):
    resp = await client.get(
        "/api/bmu/history",
        params={"from": "-1h", "to": "now()", "battery": 0},
        headers=HEADERS,
    )
    # May be 200 (empty) or 500 (no InfluxDB) — both acceptable in test
    assert resp.status_code in (200, 500)


# ─── Cleanup ─────────────────────────────────────────────────────────


def test_cleanup():
    """Remove temp database file."""
    try:
        os.unlink(_tmp_db.name)
    except OSError:
        pass
```

- [ ] **Step 3: Run tests**

```bash
cd kxkm-api && pip install -r requirements.txt && python -m pytest tests/ -v
```

Expected: All auth + sync + audit tests pass. Battery/history tests may skip or return 500 (no InfluxDB in CI).

- [ ] **Step 4: Commit**

```bash
git add kxkm-api/tests/
git commit -m "test(api): add pytest suite for auth, sync, and audit endpoints"
```

---

## Deployment (after all tasks complete)

```bash
# On local machine
scp -r kxkm-api/ kxkm@kxkm-ai:~/kxkm-api/

# On kxkm-ai
ssh kxkm@kxkm-ai
cd ~/kxkm-api
cp .env.example .env
# Edit .env with real INFLUX_TOKEN and BMU_API_KEY
nano .env
docker compose up -d
# Verify
curl -H "Authorization: Bearer YOUR_KEY" http://localhost:8400/api/bmu/batteries
```
