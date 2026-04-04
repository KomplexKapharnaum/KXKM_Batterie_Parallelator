"""Cron scheduler: runs ETL + inference every 30 minutes, writes results to InfluxDB.

Pipeline:
1. ETL: query InfluxDB -> compute feature windows per battery
2. TSMixer: score each battery (SOH, RUL, anomaly)
3. GNN: score fleet-level health
4. Write results to InfluxDB (soh_ml, soh_fleet measurements)
5. Update API cache
"""

from __future__ import annotations

import logging
import time
from datetime import datetime, timezone

from apscheduler.schedulers.blocking import BlockingScheduler
from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

from soh.api import update_cache
from soh.config import settings
from soh.etl import extract_all_batteries
from soh.inference import InferenceRunner

logger = logging.getLogger(__name__)

_runner: InferenceRunner | None = None


def get_runner() -> InferenceRunner:
    """Lazy-load inference runner."""
    global _runner
    if _runner is None:
        _runner = InferenceRunner()
    return _runner


def write_scores_to_influxdb(
    battery_scores: dict[int, dict],
    fleet_score: dict | None,
) -> None:
    """Write scoring results to InfluxDB."""
    client = InfluxDBClient(
        url=settings.influxdb_url,
        token=settings.influxdb_token,
        org=settings.influxdb_org,
    )
    write_api = client.write_api(write_options=SYNCHRONOUS)

    now = datetime.now(timezone.utc)

    # Per-battery scores
    points = []
    for bat_id, score in battery_scores.items():
        p = (
            Point("soh_ml")
            .tag("battery", str(bat_id))
            .field("soh_score", score["soh_score"])
            .field("rul_days", score["rul_days"])
            .field("anomaly_score", score["anomaly_score"])
            .time(now, WritePrecision.S)
        )
        points.append(p)

    # Fleet score
    if fleet_score:
        p = (
            Point("soh_fleet")
            .field("fleet_health", fleet_score["fleet_health"])
            .field("outlier_idx", fleet_score["outlier_idx"])
            .field("outlier_score", fleet_score["outlier_score"])
            .field("imbalance", fleet_score["imbalance_severity"])
            .time(now, WritePrecision.S)
        )
        points.append(p)

    write_api.write(bucket=settings.influxdb_output_bucket, record=points)
    client.close()
    logger.info("Wrote %d points to InfluxDB", len(points))


def run_scoring_cycle() -> dict:
    """Run one full scoring cycle: ETL -> TSMixer -> GNN -> write."""
    t0 = time.time()
    logger.info("Starting scoring cycle...")

    # Step 1: ETL
    feature_windows = extract_all_batteries()
    if not feature_windows:
        logger.warning("No batteries with sufficient data, skipping cycle")
        return {"battery_scores": {}, "fleet_score": None}

    # Convert FeatureWindow objects to numpy arrays for inference
    features = {bid: fw.matrix for bid, fw in feature_windows.items()}

    # Step 2: TSMixer per-battery scoring
    runner = get_runner()
    battery_scores = runner.score_batteries(features)
    logger.info("TSMixer scored %d batteries", len(battery_scores))

    # Step 3: GNN fleet scoring
    fleet_score = runner.score_fleet(battery_scores, features)
    if fleet_score:
        logger.info("Fleet health: %.2f, outlier: bat %d (score %.2f)",
                     fleet_score["fleet_health"],
                     fleet_score["outlier_idx"],
                     fleet_score["outlier_score"])

    # Step 4: Write to InfluxDB
    try:
        write_scores_to_influxdb(battery_scores, fleet_score)
    except Exception:
        logger.exception("Failed to write scores to InfluxDB")

    # Step 5: Update API cache
    update_cache(battery_scores, fleet_score)

    elapsed = time.time() - t0
    logger.info("Scoring cycle complete in %.1fs (%d batteries)", elapsed, len(battery_scores))

    return {"battery_scores": battery_scores, "fleet_score": fleet_score}


def main():
    """Run scheduler with APScheduler."""
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)-8s %(message)s")
    logger.info("SOH Scheduler starting (interval: %d min)", settings.scoring_interval_min)

    # Run immediately on startup
    run_scoring_cycle()

    # Schedule periodic runs
    scheduler = BlockingScheduler()
    scheduler.add_job(
        run_scoring_cycle,
        "interval",
        minutes=settings.scoring_interval_min,
        id="soh_scoring",
        max_instances=1,
    )

    try:
        scheduler.start()
    except (KeyboardInterrupt, SystemExit):
        logger.info("Scheduler stopped")


if __name__ == "__main__":
    main()
