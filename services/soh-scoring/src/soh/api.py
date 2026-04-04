"""FastAPI REST API for SOH scoring results.

Endpoints:
    GET  /api/soh/batteries          -> all battery scores (latest)
    GET  /api/soh/battery/{id}       -> single battery score + history
    GET  /api/soh/fleet              -> fleet score + outlier info
    POST /api/soh/predict            -> force refresh (on-demand inference)
    GET  /api/soh/history/{id}       -> SOH/RUL/anomaly time series
    GET  /health                     -> health check
"""

from __future__ import annotations

import logging
import time
from contextlib import asynccontextmanager
from datetime import datetime, timezone

import uvicorn
from fastapi import FastAPI, HTTPException, Query
from pydantic import BaseModel

from soh.config import settings

logger = logging.getLogger(__name__)

# In-memory cache for latest scores (populated by scheduler)
_battery_cache: dict[int, dict] = {}
_fleet_cache: dict | None = None
_last_update: float = 0.0


class BatteryScore(BaseModel):
    battery: int
    soh_score: float
    rul_days: float
    anomaly_score: float
    r_int_trend_mohm_per_day: float | None = None
    timestamp: int


class FleetScore(BaseModel):
    fleet_health: float
    outlier_idx: int
    outlier_score: float
    imbalance_severity: float
    n_batteries: int
    timestamp: int


class HealthResponse(BaseModel):
    status: str
    last_update: str | None
    n_batteries: int
    models_loaded: bool


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Startup: try to load models. Shutdown: cleanup."""
    logger.info("SOH API starting on %s:%d", settings.api_host, settings.api_port)
    yield
    logger.info("SOH API shutting down")


app = FastAPI(
    title="KXKM BMU SOH Scoring API",
    version="0.1.0",
    lifespan=lifespan,
)


def update_cache(battery_scores: dict[int, dict], fleet_score: dict | None = None):
    """Update the in-memory score cache (called by scheduler)."""
    global _battery_cache, _fleet_cache, _last_update
    _battery_cache = battery_scores
    _fleet_cache = fleet_score
    _last_update = time.time()


@app.get("/health", response_model=HealthResponse)
async def health():
    return HealthResponse(
        status="ok",
        last_update=datetime.fromtimestamp(_last_update, tz=timezone.utc).isoformat() if _last_update > 0 else None,
        n_batteries=len(_battery_cache),
        models_loaded=len(_battery_cache) > 0,
    )


@app.get("/api/soh/batteries", response_model=list[BatteryScore])
async def get_all_batteries():
    if not _battery_cache:
        return []
    ts = int(_last_update)
    return [
        BatteryScore(battery=bid, timestamp=ts, **score)
        for bid, score in sorted(_battery_cache.items())
    ]


@app.get("/api/soh/battery/{battery_id}", response_model=BatteryScore)
async def get_battery(battery_id: int):
    if battery_id not in _battery_cache:
        raise HTTPException(status_code=404, detail=f"Battery {battery_id} not found")
    ts = int(_last_update)
    return BatteryScore(battery=battery_id, timestamp=ts, **_battery_cache[battery_id])


@app.get("/api/soh/fleet", response_model=FleetScore)
async def get_fleet():
    if not _fleet_cache:
        raise HTTPException(status_code=404, detail="No fleet score available")
    return FleetScore(
        n_batteries=len(_battery_cache),
        timestamp=int(_last_update),
        **_fleet_cache,
    )


@app.post("/api/soh/predict")
async def predict():
    """Trigger on-demand inference refresh."""
    try:
        from soh.scheduler import run_scoring_cycle
        results = run_scoring_cycle()
        return {"status": "ok", "n_batteries": len(results.get("battery_scores", {}))}
    except Exception as exc:
        logger.exception("On-demand prediction failed")
        raise HTTPException(status_code=503, detail=str(exc))


@app.get("/api/soh/history/{battery_id}")
async def get_history(battery_id: int, days: int = Query(default=30, ge=1, le=365)):
    """Query historical SOH/RUL/anomaly scores from InfluxDB."""
    try:
        from influxdb_client import InfluxDBClient

        client = InfluxDBClient(
            url=settings.influxdb_url,
            token=settings.influxdb_token,
            org=settings.influxdb_org,
        )
        query_api = client.query_api()

        flux = f'''
        from(bucket: "{settings.influxdb_output_bucket}")
          |> range(start: -{days}d)
          |> filter(fn: (r) => r._measurement == "soh_ml")
          |> filter(fn: (r) => r.battery == "{battery_id}")
          |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
          |> sort(columns: ["_time"])
        '''

        tables = query_api.query(flux)
        history = []
        for table in tables:
            for record in table.records:
                history.append({
                    "timestamp": int(record.get_time().timestamp()),
                    "soh_score": record.values.get("soh_score"),
                    "rul_days": record.values.get("rul_days"),
                    "anomaly_score": record.values.get("anomaly_score"),
                })
        client.close()

        if not history:
            raise HTTPException(status_code=404, detail=f"No history for battery {battery_id}")

        return {"battery": battery_id, "days": days, "history": history}

    except HTTPException:
        raise
    except Exception as exc:
        logger.exception("History query failed")
        raise HTTPException(status_code=503, detail=str(exc))


def main():
    uvicorn.run(
        "soh.api:app",
        host=settings.api_host,
        port=settings.api_port,
        log_level="info",
    )


if __name__ == "__main__":
    main()
