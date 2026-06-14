"""InfluxDB query helpers for BMU battery data."""

from __future__ import annotations

from influxdb_client import InfluxDBClient
from influxdb_client.client.query_api import QueryApi

from .config import settings
from .models import BatteryState, ClimateState, HistoryPoint, SolarState

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


def _dev_filter(device: str | None, tag: str = "bmu") -> str:
    """Filtre Flux par device (tag `bmu` pour battery/climate, `device` pour
    solar). Sanitise les guillemets (les noms sont [A-Za-z0-9_-] côté firmware)."""
    if not device:
        return ""
    d = device.replace('"', "").replace("\\", "")
    return f'      |> filter(fn: (r) => r.{tag} == "{d}")\n'


def get_devices() -> list[str]:
    """Liste des BMU connus (valeurs distinctes du tag `bmu`)."""
    if not _query:
        return []
    flux = f"""
    import "influxdata/influxdb/schema"
    schema.tagValues(bucket: "{settings.influx_bucket}", tag: "bmu",
                     predicate: (r) => r._measurement == "battery", start: -30d)
    """
    out: list[str] = []
    try:
        for table in _query.query(flux):
            for record in table.records:
                v = record.get_value()
                if v:
                    out.append(str(v))
    except Exception:
        return []
    return sorted(out)


def get_current_batteries(device: str | None = None) -> list[BatteryState]:
    """Get latest state for each battery from InfluxDB.

    Schéma canonique (mesure `battery`), alimenté soit par l'écriture
    directe du firmware, soit par le pont Telegraf (telegraf.conf) :
      champs : voltage_mv, current_ma, state, ah_discharge_mah,
               ah_charge_mah, soh_pct, r_ohmic_mohm
      tag    : id (index 0-15), bmu (nom du device)
    """
    if not _query:
        return []

    flux = f"""
    from(bucket: "{settings.influx_bucket}")
      |> range(start: -5m)
      |> filter(fn: (r) => r._measurement == "battery")
{_dev_filter(device)}      |> last()
      |> pivot(rowKey:["_time"], columnKey:["_field"], valueColumn:"_value")
    """

    tables = _query.query(flux)
    batteries: list[BatteryState] = []

    for table in tables:
        for record in table.records:
            try:
                soh = record.values.get("soh_pct")
                r_int = record.values.get("r_ohmic_mohm")
                batteries.append(
                    BatteryState(
                        index=int(record.values.get("id", 0)),
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
                        soh_pct=float(soh) if soh is not None else None,
                        r_ohmic_mohm=(
                            float(r_int) if r_int is not None else None
                        ),
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
    device: str | None = None,
) -> list[HistoryPoint]:
    """Get time-series history from InfluxDB."""
    if not _query:
        return []

    battery_filter = ""
    if battery is not None:
        battery_filter = (
            f'      |> filter(fn: (r) => r.id == "{battery}")\n'
        )

    flux = f"""
    from(bucket: "{settings.influx_bucket}")
      |> range(start: {from_time}, stop: {to_time})
      |> filter(fn: (r) => r._measurement == "battery")
{_dev_filter(device)}{battery_filter}      |> pivot(rowKey:["_time"], columnKey:["_field"], valueColumn:"_value")
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
                        battery=int(record.values.get("id", 0)),
                        voltage_mv=int(record.values.get("voltage_mv", 0)),
                        current_ma=int(record.values.get("current_ma", 0)),
                        state=str(record.values.get("state", "UNKNOWN")),
                    )
                )
            except (TypeError, ValueError):
                continue

    return points


def get_latest_climate(device: str | None = None) -> ClimateState | None:
    """Latest temperature/humidity from InfluxDB (measurement `climate`)."""
    if not _query:
        return None
    flux = f"""
    from(bucket: "{settings.influx_bucket}")
      |> range(start: -1h)
      |> filter(fn: (r) => r._measurement == "climate")
{_dev_filter(device)}      |> last()
      |> pivot(rowKey:["_time"], columnKey:["_field"], valueColumn:"_value")
    """
    for table in _query.query(flux):
        for record in table.records:
            return ClimateState(
                temperature_c=float(record.values.get("temperature_c", 0.0)),
                humidity_pct=float(record.values.get("humidity_pct", 0.0)),
                timestamp=record.get_time().isoformat(),
            )
    return None


def get_latest_solar(device: str | None = None) -> SolarState | None:
    """Latest solar (VE.Direct) telemetry from InfluxDB (measurement `solar`)."""
    if not _query:
        return None
    flux = f"""
    from(bucket: "{settings.influx_bucket}")
      |> range(start: -1h)
      |> filter(fn: (r) => r._measurement == "solar")
{_dev_filter(device, "device")}      |> last()
      |> pivot(rowKey:["_time"], columnKey:["_field"], valueColumn:"_value")
    """
    for table in _query.query(flux):
        for record in table.records:
            v = record.values
            return SolarState(
                pv_voltage_v=float(v.get("vpv", 0.0)),
                pv_power_w=float(v.get("ppv", 0.0)),
                battery_voltage_v=float(v.get("vbat", 0.0)),
                battery_current_a=float(v.get("ibat", 0.0)),
                charge_state=str(v.get("cs", "")),
                yield_today_wh=float(v.get("yield", 0.0)),
                error_code=int(v.get("err", 0)),
                timestamp=record.get_time().isoformat(),
            )
    return None


def get_series(
    measurement: str,
    fields: list[str],
    from_time: str,
    to_time: str,
    device: str | None = None,
    device_tag: str = "bmu",
) -> list[dict]:
    """Generic time-series (time + requested fields) for charts."""
    if not _query:
        return []
    fset = "[" + ", ".join(f'"{f}"' for f in fields) + "]"
    flux = f"""
    from(bucket: "{settings.influx_bucket}")
      |> range(start: {from_time}, stop: {to_time})
      |> filter(fn: (r) => r._measurement == "{measurement}")
{_dev_filter(device, device_tag)}      |> filter(fn: (r) => contains(value: r._field, set: {fset}))
      |> pivot(rowKey:["_time"], columnKey:["_field"], valueColumn:"_value")
      |> sort(columns: ["_time"])
      |> limit(n: 5000)
    """
    out: list[dict] = []
    for table in _query.query(flux):
        for record in table.records:
            row: dict = {"time": record.get_time().isoformat()}
            for f in fields:
                val = record.values.get(f)
                if val is not None:
                    row[f] = val
            out.append(row)
    return out
