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
