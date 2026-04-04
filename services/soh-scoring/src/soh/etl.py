"""ETL: InfluxDB -> feature windows per battery.

Queries InfluxDB for the last N days of `rint` and `battery` measurements,
computes windowed features per battery, and returns a dict of FeatureWindow objects.
"""

from __future__ import annotations

import logging
from datetime import datetime, timezone

import numpy as np
from influxdb_client import InfluxDBClient

from soh.config import settings
from soh.features import FeatureWindow, compute_battery_features

logger = logging.getLogger(__name__)


def query_battery_data(
    battery_id: int,
    days: int = 7,
    client: InfluxDBClient | None = None,
) -> dict[str, np.ndarray]:
    """Query InfluxDB for raw battery + rint data.

    Returns dict with keys: timestamps_s, voltages_mv, currents_a, r_int_mohm, die_temp_c
    """
    own_client = client is None
    if own_client:
        client = InfluxDBClient(
            url=settings.influxdb_url,
            token=settings.influxdb_token,
            org=settings.influxdb_org,
        )

    try:
        query_api = client.query_api()

        # Query battery voltage/current data
        flux_battery = f'''
        from(bucket: "{settings.influxdb_bucket}")
          |> range(start: -{days}d)
          |> filter(fn: (r) => r._measurement == "battery")
          |> filter(fn: (r) => r.battery == "{battery_id}")
          |> filter(fn: (r) => r._field == "voltage" or r._field == "current" or r._field == "die_temp")
          |> pivot(rowKey: ["_time"], columnKey: ["_field"], valueColumn: "_value")
          |> sort(columns: ["_time"])
        '''

        # Query R_int data
        flux_rint = f'''
        from(bucket: "{settings.influxdb_bucket}")
          |> range(start: -{days}d)
          |> filter(fn: (r) => r._measurement == "rint")
          |> filter(fn: (r) => r.battery == "{battery_id}")
          |> filter(fn: (r) => r._field == "r_ohmic_mohm")
          |> sort(columns: ["_time"])
        '''

        # Parse battery data
        tables_bat = query_api.query(flux_battery)
        timestamps, voltages, currents, temps = [], [], [], []
        for table in tables_bat:
            for record in table.records:
                ts = record.get_time().timestamp()
                timestamps.append(ts)
                voltages.append(record.values.get("voltage", 0.0))
                currents.append(record.values.get("current", 0.0))
                temps.append(record.values.get("die_temp", 35.0))

        # Parse R_int data — interpolate to battery timestamps
        tables_rint = query_api.query(flux_rint)
        rint_ts, rint_vals = [], []
        for table in tables_rint:
            for record in table.records:
                rint_ts.append(record.get_time().timestamp())
                rint_vals.append(record.get_value())

        # Interpolate R_int to battery timestamps
        if rint_ts and timestamps:
            r_int_interp = np.interp(timestamps, rint_ts, rint_vals)
        else:
            r_int_interp = np.full(len(timestamps), np.nan)

        return {
            "timestamps_s": np.array(timestamps, dtype=np.float64),
            "voltages_mv": np.array(voltages, dtype=np.float32),
            "currents_a": np.array(currents, dtype=np.float32),
            "r_int_mohm": r_int_interp.astype(np.float32),
            "die_temp_c": np.array(temps, dtype=np.float32),
        }
    finally:
        if own_client:
            client.close()


def extract_all_batteries(
    n_batteries: int | None = None,
    days: int | None = None,
) -> dict[int, FeatureWindow]:
    """Run full ETL: query InfluxDB -> compute features for all batteries.

    Returns dict mapping battery_id -> FeatureWindow.
    """
    days = days or settings.etl_window_days
    n_batteries = n_batteries or settings.max_batteries

    client = InfluxDBClient(
        url=settings.influxdb_url,
        token=settings.influxdb_token,
        org=settings.influxdb_org,
    )

    results: dict[int, FeatureWindow] = {}
    try:
        for bat_id in range(n_batteries):
            try:
                data = query_battery_data(bat_id, days=days, client=client)
                if len(data["timestamps_s"]) < 60:  # need at least 1 hour of data
                    logger.warning("Battery %d: insufficient data (%d points), skipping",
                                   bat_id, len(data["timestamps_s"]))
                    continue

                fw = compute_battery_features(
                    timestamps_s=data["timestamps_s"],
                    voltages_mv=data["voltages_mv"],
                    currents_a=data["currents_a"],
                    r_int_mohm=data["r_int_mohm"],
                    die_temp_c=data["die_temp_c"],
                    window_min=settings.etl_sample_interval_min,
                )
                fw.battery_id = bat_id
                results[bat_id] = fw
                logger.info("Battery %d: %d windows extracted", bat_id, fw.matrix.shape[0])
            except Exception:
                logger.exception("Battery %d: ETL failed", bat_id)
    finally:
        client.close()

    logger.info("ETL complete: %d batteries with features", len(results))
    return results
