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
