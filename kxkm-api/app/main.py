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
