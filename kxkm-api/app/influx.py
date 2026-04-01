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
