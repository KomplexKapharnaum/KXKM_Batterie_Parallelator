package com.kxkm.bmu.db.shared

import app.cash.sqldelight.TransacterImpl
import app.cash.sqldelight.db.AfterVersion
import app.cash.sqldelight.db.QueryResult
import app.cash.sqldelight.db.SqlDriver
import app.cash.sqldelight.db.SqlSchema
import com.kxkm.bmu.db.BmuDatabase
import com.kxkm.bmu.db.BmuDatabaseQueries
import kotlin.Long
import kotlin.Unit
import kotlin.reflect.KClass

internal val KClass<BmuDatabase>.schema: SqlSchema<QueryResult.Value<Unit>>
  get() = BmuDatabaseImpl.Schema

internal fun KClass<BmuDatabase>.newInstance(driver: SqlDriver): BmuDatabase =
    BmuDatabaseImpl(driver)

private class BmuDatabaseImpl(
  driver: SqlDriver,
) : TransacterImpl(driver), BmuDatabase {
  override val bmuDatabaseQueries: BmuDatabaseQueries = BmuDatabaseQueries(driver)

  public object Schema : SqlSchema<QueryResult.Value<Unit>> {
    override val version: Long
      get() = 1

    override fun create(driver: SqlDriver): QueryResult.Value<Unit> {
      driver.execute(null, """
          |CREATE TABLE battery_history (
          |    id INTEGER PRIMARY KEY AUTOINCREMENT,
          |    timestamp INTEGER NOT NULL,
          |    battery_index INTEGER NOT NULL,
          |    voltage_mv INTEGER NOT NULL,
          |    current_ma INTEGER NOT NULL,
          |    state TEXT NOT NULL,
          |    ah_discharge_mah INTEGER NOT NULL DEFAULT 0,
          |    ah_charge_mah INTEGER NOT NULL DEFAULT 0
          |)
          """.trimMargin(), 0)
      driver.execute(null, """
          |CREATE TABLE audit_events (
          |    id INTEGER PRIMARY KEY AUTOINCREMENT,
          |    timestamp INTEGER NOT NULL,
          |    user_id TEXT NOT NULL,
          |    action TEXT NOT NULL,
          |    target INTEGER,
          |    detail TEXT,
          |    synced INTEGER NOT NULL DEFAULT 0
          |)
          """.trimMargin(), 0)
      driver.execute(null, """
          |CREATE TABLE user_profiles (
          |    id TEXT PRIMARY KEY,
          |    name TEXT NOT NULL,
          |    role TEXT NOT NULL,
          |    pin_hash TEXT NOT NULL,
          |    salt TEXT NOT NULL
          |)
          """.trimMargin(), 0)
      driver.execute(null, """
          |CREATE TABLE sync_queue (
          |    id INTEGER PRIMARY KEY AUTOINCREMENT,
          |    type TEXT NOT NULL,
          |    payload TEXT NOT NULL,
          |    created_at INTEGER NOT NULL,
          |    retry_count INTEGER NOT NULL DEFAULT 0
          |)
          """.trimMargin(), 0)
      driver.execute(null, """
          |CREATE TABLE ml_scores (
          |    id INTEGER PRIMARY KEY AUTOINCREMENT,
          |    battery_index INTEGER NOT NULL,
          |    soh_score REAL NOT NULL,
          |    rul_days INTEGER NOT NULL,
          |    anomaly_score REAL NOT NULL,
          |    r_int_trend REAL NOT NULL DEFAULT 0.0,
          |    timestamp INTEGER NOT NULL
          |)
          """.trimMargin(), 0)
      driver.execute(null, """
          |CREATE TABLE fleet_health (
          |    id INTEGER PRIMARY KEY AUTOINCREMENT,
          |    fleet_health_score REAL NOT NULL,
          |    outlier_idx INTEGER NOT NULL,
          |    outlier_score REAL NOT NULL,
          |    imbalance_severity REAL NOT NULL DEFAULT 0.0,
          |    timestamp INTEGER NOT NULL
          |)
          """.trimMargin(), 0)
      driver.execute(null, """
          |CREATE TABLE diagnostics (
          |    id INTEGER PRIMARY KEY AUTOINCREMENT,
          |    battery_index INTEGER NOT NULL,
          |    diagnostic_text TEXT NOT NULL,
          |    severity TEXT NOT NULL,
          |    generated_at INTEGER NOT NULL
          |)
          """.trimMargin(), 0)
      driver.execute(null, "CREATE INDEX idx_history_time ON battery_history(timestamp)", 0)
      driver.execute(null,
          "CREATE INDEX idx_history_battery ON battery_history(battery_index, timestamp)", 0)
      driver.execute(null, "CREATE INDEX idx_audit_time ON audit_events(timestamp)", 0)
      driver.execute(null, "CREATE INDEX idx_audit_synced ON audit_events(synced)", 0)
      driver.execute(null,
          "CREATE INDEX idx_ml_scores_battery ON ml_scores(battery_index, timestamp)", 0)
      driver.execute(null, "CREATE INDEX idx_fleet_time ON fleet_health(timestamp)", 0)
      driver.execute(null,
          "CREATE INDEX idx_diag_battery ON diagnostics(battery_index, generated_at)", 0)
      return QueryResult.Unit
    }

    override fun migrate(
      driver: SqlDriver,
      oldVersion: Long,
      newVersion: Long,
      vararg callbacks: AfterVersion,
    ): QueryResult.Value<Unit> = QueryResult.Unit
  }
}
