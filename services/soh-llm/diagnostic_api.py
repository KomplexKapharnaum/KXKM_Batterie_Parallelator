#!/usr/bin/env python3
"""
diagnostic_api.py — FastAPI diagnostic service for battery health narratives.

Endpoints:
    GET  /api/diagnostic/{battery_id}    — cached daily diagnostic
    POST /api/diagnostic/{battery_id}    — generate fresh diagnostic (on-demand)
    GET  /api/diagnostic/fleet           — fleet-level summary
    GET  /health                         — health check

Scheduling:
    Daily digest at 06:00 — generates diagnostics for all batteries.
"""

from __future__ import annotations

import logging
import time
from contextlib import asynccontextmanager
from datetime import datetime
from typing import Any

import httpx
from apscheduler.schedulers.asyncio import AsyncIOScheduler
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel

from config import config
from inference_server import generate_diagnostic, load_model

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("api")

# ---------------------------------------------------------------------------
# Cache: battery_id -> {diagnostic, severity, generated_at}
# ---------------------------------------------------------------------------
_cache: dict[int, dict[str, Any]] = {}
_fleet_cache: dict[str, Any] | None = None


# ---------------------------------------------------------------------------
# SOH API client (Phase 2)
# ---------------------------------------------------------------------------

async def fetch_battery_scores(battery_id: int) -> dict[str, Any]:
    """Fetch ML scores from Phase 2 SOH API."""
    url = f"http://localhost:8400/api/soh/battery/{battery_id}"
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.get(url)
        if resp.status_code != 200:
            raise HTTPException(status_code=502, detail=f"SOH API returned {resp.status_code}")
        return resp.json()


async def fetch_fleet_scores() -> dict[str, Any]:
    """Fetch fleet scores from Phase 2 SOH API."""
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.get("http://localhost:8400/api/soh/fleet")
        if resp.status_code != 200:
            raise HTTPException(status_code=502, detail=f"SOH API returned {resp.status_code}")
        return resp.json()


async def fetch_all_battery_scores() -> list[dict[str, Any]]:
    """Fetch scores for all batteries from Phase 2 SOH API."""
    async with httpx.AsyncClient(timeout=10.0) as client:
        resp = await client.get("http://localhost:8400/api/soh/batteries")
        if resp.status_code != 200:
            raise HTTPException(status_code=502, detail=f"SOH API returned {resp.status_code}")
        return resp.json()


# ---------------------------------------------------------------------------
# Daily digest job
# ---------------------------------------------------------------------------

async def daily_digest() -> None:
    """Generate diagnostics for all batteries (cron job)."""
    global _fleet_cache
    log.info("Starting daily digest...")
    try:
        batteries = await fetch_all_battery_scores()
        for scores in batteries:
            bid = scores.get("battery", -1)
            try:
                result = generate_diagnostic(scores)
                _cache[bid] = {
                    **result,
                    "generated_at": int(time.time()),
                }
            except Exception as e:
                log.error("Digest failed for battery %d: %s", bid, e)

        # Fleet summary
        fleet = await fetch_fleet_scores()
        fleet_diag = generate_diagnostic({
            "battery": -1,
            "fleet_size": len(batteries),
            "soh_score": fleet.get("fleet_health", 0) * 100,
            "rul_days": min((b.get("rul_days", 999) for b in batteries), default=0),
            "anomaly_score": fleet.get("outlier_score", 0),
            "r_ohmic_mohm": "N/A",
            "r_total_mohm": "N/A",
            "r_int_trend_mohm_per_day": "N/A",
            "v_avg_mv": "N/A",
            "i_avg_a": "N/A",
            "cycle_count": "N/A",
            "fleet_health_pct": fleet.get("fleet_health", 0) * 100,
        })
        _fleet_cache = {
            **fleet_diag,
            "generated_at": int(time.time()),
            "num_batteries": len(batteries),
        }
        log.info("Daily digest complete: %d batteries", len(batteries))
    except Exception as e:
        log.error("Daily digest failed: %s", e)


# ---------------------------------------------------------------------------
# App lifecycle
# ---------------------------------------------------------------------------

@asynccontextmanager
async def lifespan(app: FastAPI):
    """Load model on startup, schedule daily digest."""
    load_model()

    scheduler = AsyncIOScheduler()
    scheduler.add_job(
        daily_digest,
        "cron",
        hour=config.daily_digest_hour,
        minute=0,
        id="daily_digest",
    )
    scheduler.start()
    log.info("Scheduled daily digest at %02d:00", config.daily_digest_hour)

    yield

    scheduler.shutdown()


app = FastAPI(
    title="BMU LLM Diagnostic API",
    version="1.0.0",
    lifespan=lifespan,
)


# ---------------------------------------------------------------------------
# Response models
# ---------------------------------------------------------------------------

class DiagnosticResponse(BaseModel):
    battery: int
    diagnostic: str
    severity: str
    generated_at: int


class FleetDiagnosticResponse(BaseModel):
    diagnostic: str
    severity: str
    generated_at: int
    num_batteries: int


class HealthResponse(BaseModel):
    status: str
    model_loaded: bool
    cache_size: int
    uptime_s: float


_start_time = time.monotonic()


# ---------------------------------------------------------------------------
# Routes
# ---------------------------------------------------------------------------

@app.get("/health", response_model=HealthResponse)
async def health_check():
    from inference_server import _model
    return HealthResponse(
        status="ok",
        model_loaded=_model is not None,
        cache_size=len(_cache),
        uptime_s=round(time.monotonic() - _start_time, 1),
    )


@app.get("/api/diagnostic/fleet", response_model=FleetDiagnosticResponse)
async def get_fleet_diagnostic():
    """Return cached fleet diagnostic (from daily digest)."""
    if _fleet_cache is None:
        raise HTTPException(status_code=404, detail="No fleet diagnostic available. Wait for daily digest or POST to generate.")
    return FleetDiagnosticResponse(**_fleet_cache)


@app.get("/api/diagnostic/{battery_id}", response_model=DiagnosticResponse)
async def get_battery_diagnostic(battery_id: int):
    """Return cached daily diagnostic for a battery."""
    if battery_id not in _cache:
        raise HTTPException(status_code=404, detail=f"No cached diagnostic for battery {battery_id}. POST to generate on-demand.")
    entry = _cache[battery_id]
    # Check staleness
    age_h = (time.time() - entry["generated_at"]) / 3600
    if age_h > config.cache_ttl_hours:
        raise HTTPException(status_code=404, detail=f"Cached diagnostic expired ({age_h:.1f}h old). POST to refresh.")
    return DiagnosticResponse(**entry)


@app.post("/api/diagnostic/{battery_id}", response_model=DiagnosticResponse)
async def generate_battery_diagnostic(battery_id: int):
    """Generate a fresh diagnostic on-demand for a battery."""
    scores = await fetch_battery_scores(battery_id)
    result = generate_diagnostic(scores)
    entry = {
        **result,
        "generated_at": int(time.time()),
    }
    _cache[battery_id] = entry
    return DiagnosticResponse(**entry)


# ---------------------------------------------------------------------------
# Entrypoint
# ---------------------------------------------------------------------------

def main() -> None:
    import uvicorn
    uvicorn.run(
        "diagnostic_api:app",
        host=config.api_host,
        port=config.api_port,
        workers=1,  # Single worker — model in GPU memory
        log_level="info",
    )


if __name__ == "__main__":
    main()
