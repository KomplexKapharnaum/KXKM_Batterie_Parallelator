package com.kxkm.bmu.db

import app.cash.sqldelight.Query
import app.cash.sqldelight.TransacterImpl
import app.cash.sqldelight.db.QueryResult
import app.cash.sqldelight.db.SqlCursor
import app.cash.sqldelight.db.SqlDriver
import kotlin.Any
import kotlin.Double
import kotlin.Long
import kotlin.String

public class BmuDatabaseQueries(
  driver: SqlDriver,
) : TransacterImpl(driver) {
  public fun <T : Any> getHistory(
    battery_index: Long,
    timestamp: Long,
    mapper: (
      id: Long,
      timestamp: Long,
      battery_index: Long,
      voltage_mv: Long,
      current_ma: Long,
      state: String,
      ah_discharge_mah: Long,
      ah_charge_mah: Long,
    ) -> T,
  ): Query<T> = GetHistoryQuery(battery_index, timestamp) { cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getLong(1)!!,
      cursor.getLong(2)!!,
      cursor.getLong(3)!!,
      cursor.getLong(4)!!,
      cursor.getString(5)!!,
      cursor.getLong(6)!!,
      cursor.getLong(7)!!
    )
  }

  public fun getHistory(battery_index: Long, timestamp: Long): Query<Battery_history> =
      getHistory(battery_index, timestamp) { id, timestamp_, battery_index_, voltage_mv, current_ma,
      state, ah_discharge_mah, ah_charge_mah ->
    Battery_history(
      id,
      timestamp_,
      battery_index_,
      voltage_mv,
      current_ma,
      state,
      ah_discharge_mah,
      ah_charge_mah
    )
  }

  public fun <T : Any> getLastKnownBatteries(mapper: (
    id: Long,
    timestamp: Long,
    battery_index: Long,
    voltage_mv: Long,
    current_ma: Long,
    state: String,
    ah_discharge_mah: Long,
    ah_charge_mah: Long,
  ) -> T): Query<T> = Query(-802_980_793, arrayOf("battery_history"), driver, "BmuDatabase.sq",
      "getLastKnownBatteries", """
  |SELECT battery_history.id, battery_history.timestamp, battery_history.battery_index, battery_history.voltage_mv, battery_history.current_ma, battery_history.state, battery_history.ah_discharge_mah, battery_history.ah_charge_mah FROM battery_history
  |WHERE id IN (
  |    SELECT MAX(id)
  |    FROM battery_history
  |    GROUP BY battery_index
  |)
  |ORDER BY battery_index ASC
  """.trimMargin()) { cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getLong(1)!!,
      cursor.getLong(2)!!,
      cursor.getLong(3)!!,
      cursor.getLong(4)!!,
      cursor.getString(5)!!,
      cursor.getLong(6)!!,
      cursor.getLong(7)!!
    )
  }

  public fun getLastKnownBatteries(): Query<Battery_history> = getLastKnownBatteries { id,
      timestamp, battery_index, voltage_mv, current_ma, state, ah_discharge_mah, ah_charge_mah ->
    Battery_history(
      id,
      timestamp,
      battery_index,
      voltage_mv,
      current_ma,
      state,
      ah_discharge_mah,
      ah_charge_mah
    )
  }

  public fun <T : Any> getAuditEvents(`value`: Long, mapper: (
    id: Long,
    timestamp: Long,
    user_id: String,
    action: String,
    target: Long?,
    detail: String?,
    synced: Long,
  ) -> T): Query<T> = GetAuditEventsQuery(value) { cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getLong(1)!!,
      cursor.getString(2)!!,
      cursor.getString(3)!!,
      cursor.getLong(4),
      cursor.getString(5),
      cursor.getLong(6)!!
    )
  }

  public fun getAuditEvents(value_: Long): Query<Audit_events> = getAuditEvents(value_) { id,
      timestamp, user_id, action, target, detail, synced ->
    Audit_events(
      id,
      timestamp,
      user_id,
      action,
      target,
      detail,
      synced
    )
  }

  public fun <T : Any> getAuditByAction(
    action: String,
    `value`: Long,
    mapper: (
      id: Long,
      timestamp: Long,
      user_id: String,
      action: String,
      target: Long?,
      detail: String?,
      synced: Long,
    ) -> T,
  ): Query<T> = GetAuditByActionQuery(action, value) { cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getLong(1)!!,
      cursor.getString(2)!!,
      cursor.getString(3)!!,
      cursor.getLong(4),
      cursor.getString(5),
      cursor.getLong(6)!!
    )
  }

  public fun getAuditByAction(action: String, value_: Long): Query<Audit_events> =
      getAuditByAction(action, value_) { id, timestamp, user_id, action_, target, detail, synced ->
    Audit_events(
      id,
      timestamp,
      user_id,
      action_,
      target,
      detail,
      synced
    )
  }

  public fun <T : Any> getAuditByTarget(
    target: Long?,
    `value`: Long,
    mapper: (
      id: Long,
      timestamp: Long,
      user_id: String,
      action: String,
      target: Long?,
      detail: String?,
      synced: Long,
    ) -> T,
  ): Query<T> = GetAuditByTargetQuery(target, value) { cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getLong(1)!!,
      cursor.getString(2)!!,
      cursor.getString(3)!!,
      cursor.getLong(4),
      cursor.getString(5),
      cursor.getLong(6)!!
    )
  }

  public fun getAuditByTarget(target: Long?, value_: Long): Query<Audit_events> =
      getAuditByTarget(target, value_) { id, timestamp, user_id, action, target_, detail, synced ->
    Audit_events(
      id,
      timestamp,
      user_id,
      action,
      target_,
      detail,
      synced
    )
  }

  public fun <T : Any> getAuditByActionAndTarget(
    action: String,
    target: Long?,
    `value`: Long,
    mapper: (
      id: Long,
      timestamp: Long,
      user_id: String,
      action: String,
      target: Long?,
      detail: String?,
      synced: Long,
    ) -> T,
  ): Query<T> = GetAuditByActionAndTargetQuery(action, target, value) { cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getLong(1)!!,
      cursor.getString(2)!!,
      cursor.getString(3)!!,
      cursor.getLong(4),
      cursor.getString(5),
      cursor.getLong(6)!!
    )
  }

  public fun getAuditByActionAndTarget(
    action: String,
    target: Long?,
    value_: Long,
  ): Query<Audit_events> = getAuditByActionAndTarget(action, target, value_) { id, timestamp,
      user_id, action_, target_, detail, synced ->
    Audit_events(
      id,
      timestamp,
      user_id,
      action_,
      target_,
      detail,
      synced
    )
  }

  public fun <T : Any> getUnsyncedAudit(`value`: Long, mapper: (
    id: Long,
    timestamp: Long,
    user_id: String,
    action: String,
    target: Long?,
    detail: String?,
    synced: Long,
  ) -> T): Query<T> = GetUnsyncedAuditQuery(value) { cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getLong(1)!!,
      cursor.getString(2)!!,
      cursor.getString(3)!!,
      cursor.getLong(4),
      cursor.getString(5),
      cursor.getLong(6)!!
    )
  }

  public fun getUnsyncedAudit(value_: Long): Query<Audit_events> = getUnsyncedAudit(value_) { id,
      timestamp, user_id, action, target, detail, synced ->
    Audit_events(
      id,
      timestamp,
      user_id,
      action,
      target,
      detail,
      synced
    )
  }

  public fun countUnsyncedAudit(): Query<Long> = Query(-173_419_354, arrayOf("audit_events"),
      driver, "BmuDatabase.sq", "countUnsyncedAudit",
      "SELECT COUNT(*) FROM audit_events WHERE synced = 0") { cursor ->
    cursor.getLong(0)!!
  }

  public fun <T : Any> getUserByHash(pin_hash: String, mapper: (
    id: String,
    name: String,
    role: String,
    pin_hash: String,
    salt: String,
  ) -> T): Query<T> = GetUserByHashQuery(pin_hash) { cursor ->
    mapper(
      cursor.getString(0)!!,
      cursor.getString(1)!!,
      cursor.getString(2)!!,
      cursor.getString(3)!!,
      cursor.getString(4)!!
    )
  }

  public fun getUserByHash(pin_hash: String): Query<User_profiles> = getUserByHash(pin_hash) { id,
      name, role, pin_hash_, salt ->
    User_profiles(
      id,
      name,
      role,
      pin_hash_,
      salt
    )
  }

  public fun <T : Any> getAllUsers(mapper: (
    id: String,
    name: String,
    role: String,
    pin_hash: String,
    salt: String,
  ) -> T): Query<T> = Query(-2_096_651_056, arrayOf("user_profiles"), driver, "BmuDatabase.sq",
      "getAllUsers",
      "SELECT user_profiles.id, user_profiles.name, user_profiles.role, user_profiles.pin_hash, user_profiles.salt FROM user_profiles ORDER BY name") {
      cursor ->
    mapper(
      cursor.getString(0)!!,
      cursor.getString(1)!!,
      cursor.getString(2)!!,
      cursor.getString(3)!!,
      cursor.getString(4)!!
    )
  }

  public fun getAllUsers(): Query<User_profiles> = getAllUsers { id, name, role, pin_hash, salt ->
    User_profiles(
      id,
      name,
      role,
      pin_hash,
      salt
    )
  }

  public fun countUsers(): Query<Long> = Query(-1_513_874_074, arrayOf("user_profiles"), driver,
      "BmuDatabase.sq", "countUsers", "SELECT COUNT(*) FROM user_profiles") { cursor ->
    cursor.getLong(0)!!
  }

  public fun <T : Any> dequeueSync(`value`: Long, mapper: (
    id: Long,
    type: String,
    payload: String,
    created_at: Long,
    retry_count: Long,
  ) -> T): Query<T> = DequeueSyncQuery(value) { cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getString(1)!!,
      cursor.getString(2)!!,
      cursor.getLong(3)!!,
      cursor.getLong(4)!!
    )
  }

  public fun dequeueSync(value_: Long): Query<Sync_queue> = dequeueSync(value_) { id, type, payload,
      created_at, retry_count ->
    Sync_queue(
      id,
      type,
      payload,
      created_at,
      retry_count
    )
  }

  public fun countPendingSync(): Query<Long> = Query(1_599_833_520, arrayOf("sync_queue"), driver,
      "BmuDatabase.sq", "countPendingSync", "SELECT COUNT(*) FROM sync_queue") { cursor ->
    cursor.getLong(0)!!
  }

  public fun <T : Any> getLatestMlScores(mapper: (
    id: Long,
    battery_index: Long,
    soh_score: Double,
    rul_days: Long,
    anomaly_score: Double,
    r_int_trend: Double,
    timestamp: Long,
  ) -> T): Query<T> = Query(687_892_592, arrayOf("ml_scores"), driver, "BmuDatabase.sq",
      "getLatestMlScores",
      "SELECT ml_scores.id, ml_scores.battery_index, ml_scores.soh_score, ml_scores.rul_days, ml_scores.anomaly_score, ml_scores.r_int_trend, ml_scores.timestamp FROM ml_scores ORDER BY battery_index ASC") {
      cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getLong(1)!!,
      cursor.getDouble(2)!!,
      cursor.getLong(3)!!,
      cursor.getDouble(4)!!,
      cursor.getDouble(5)!!,
      cursor.getLong(6)!!
    )
  }

  public fun getLatestMlScores(): Query<Ml_scores> = getLatestMlScores { id, battery_index,
      soh_score, rul_days, anomaly_score, r_int_trend, timestamp ->
    Ml_scores(
      id,
      battery_index,
      soh_score,
      rul_days,
      anomaly_score,
      r_int_trend,
      timestamp
    )
  }

  public fun <T : Any> getMlScore(battery_index: Long, mapper: (
    id: Long,
    battery_index: Long,
    soh_score: Double,
    rul_days: Long,
    anomaly_score: Double,
    r_int_trend: Double,
    timestamp: Long,
  ) -> T): Query<T> = GetMlScoreQuery(battery_index) { cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getLong(1)!!,
      cursor.getDouble(2)!!,
      cursor.getLong(3)!!,
      cursor.getDouble(4)!!,
      cursor.getDouble(5)!!,
      cursor.getLong(6)!!
    )
  }

  public fun getMlScore(battery_index: Long): Query<Ml_scores> = getMlScore(battery_index) { id,
      battery_index_, soh_score, rul_days, anomaly_score, r_int_trend, timestamp ->
    Ml_scores(
      id,
      battery_index_,
      soh_score,
      rul_days,
      anomaly_score,
      r_int_trend,
      timestamp
    )
  }

  public fun <T : Any> getLatestFleetHealth(mapper: (
    id: Long,
    fleet_health_score: Double,
    outlier_idx: Long,
    outlier_score: Double,
    imbalance_severity: Double,
    timestamp: Long,
  ) -> T): Query<T> = Query(1_368_906_106, arrayOf("fleet_health"), driver, "BmuDatabase.sq",
      "getLatestFleetHealth",
      "SELECT fleet_health.id, fleet_health.fleet_health_score, fleet_health.outlier_idx, fleet_health.outlier_score, fleet_health.imbalance_severity, fleet_health.timestamp FROM fleet_health ORDER BY timestamp DESC LIMIT 1") {
      cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getDouble(1)!!,
      cursor.getLong(2)!!,
      cursor.getDouble(3)!!,
      cursor.getDouble(4)!!,
      cursor.getLong(5)!!
    )
  }

  public fun getLatestFleetHealth(): Query<Fleet_health> = getLatestFleetHealth { id,
      fleet_health_score, outlier_idx, outlier_score, imbalance_severity, timestamp ->
    Fleet_health(
      id,
      fleet_health_score,
      outlier_idx,
      outlier_score,
      imbalance_severity,
      timestamp
    )
  }

  public fun <T : Any> getDiagnostic(battery_index: Long, mapper: (
    id: Long,
    battery_index: Long,
    diagnostic_text: String,
    severity: String,
    generated_at: Long,
  ) -> T): Query<T> = GetDiagnosticQuery(battery_index) { cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getLong(1)!!,
      cursor.getString(2)!!,
      cursor.getString(3)!!,
      cursor.getLong(4)!!
    )
  }

  public fun getDiagnostic(battery_index: Long): Query<Diagnostics> = getDiagnostic(battery_index) {
      id, battery_index_, diagnostic_text, severity, generated_at ->
    Diagnostics(
      id,
      battery_index_,
      diagnostic_text,
      severity,
      generated_at
    )
  }

  public fun <T : Any> getAllDiagnostics(mapper: (
    id: Long,
    battery_index: Long,
    diagnostic_text: String,
    severity: String,
    generated_at: Long,
  ) -> T): Query<T> = Query(-529_171_852, arrayOf("diagnostics"), driver, "BmuDatabase.sq",
      "getAllDiagnostics",
      "SELECT diagnostics.id, diagnostics.battery_index, diagnostics.diagnostic_text, diagnostics.severity, diagnostics.generated_at FROM diagnostics ORDER BY battery_index ASC") {
      cursor ->
    mapper(
      cursor.getLong(0)!!,
      cursor.getLong(1)!!,
      cursor.getString(2)!!,
      cursor.getString(3)!!,
      cursor.getLong(4)!!
    )
  }

  public fun getAllDiagnostics(): Query<Diagnostics> = getAllDiagnostics { id, battery_index,
      diagnostic_text, severity, generated_at ->
    Diagnostics(
      id,
      battery_index,
      diagnostic_text,
      severity,
      generated_at
    )
  }

  public fun insertHistory(
    timestamp: Long,
    battery_index: Long,
    voltage_mv: Long,
    current_ma: Long,
    state: String,
    ah_discharge_mah: Long,
    ah_charge_mah: Long,
  ) {
    driver.execute(-1_072_792_242, """
        |INSERT INTO battery_history (timestamp, battery_index, voltage_mv, current_ma, state, ah_discharge_mah, ah_charge_mah)
        |VALUES (?, ?, ?, ?, ?, ?, ?)
        """.trimMargin(), 7) {
          bindLong(0, timestamp)
          bindLong(1, battery_index)
          bindLong(2, voltage_mv)
          bindLong(3, current_ma)
          bindString(4, state)
          bindLong(5, ah_discharge_mah)
          bindLong(6, ah_charge_mah)
        }
    notifyQueries(-1_072_792_242) { emit ->
      emit("battery_history")
    }
  }

  public fun purgeOldHistory(timestamp: Long) {
    driver.execute(-1_206_104_981, """DELETE FROM battery_history WHERE timestamp < ?""", 1) {
          bindLong(0, timestamp)
        }
    notifyQueries(-1_206_104_981) { emit ->
      emit("battery_history")
    }
  }

  public fun insertAudit(
    timestamp: Long,
    user_id: String,
    action: String,
    target: Long?,
    detail: String?,
  ) {
    driver.execute(-1_799_415_019, """
        |INSERT INTO audit_events (timestamp, user_id, action, target, detail)
        |VALUES (?, ?, ?, ?, ?)
        """.trimMargin(), 5) {
          bindLong(0, timestamp)
          bindString(1, user_id)
          bindString(2, action)
          bindLong(3, target)
          bindString(4, detail)
        }
    notifyQueries(-1_799_415_019) { emit ->
      emit("audit_events")
    }
  }

  public fun markAuditSynced(id: Long) {
    driver.execute(-833_214_437, """UPDATE audit_events SET synced = 1 WHERE id = ?""", 1) {
          bindLong(0, id)
        }
    notifyQueries(-833_214_437) { emit ->
      emit("audit_events")
    }
  }

  public fun insertUser(
    id: String,
    name: String,
    role: String,
    pin_hash: String,
    salt: String,
  ) {
    driver.execute(496_737_617,
        """INSERT INTO user_profiles (id, name, role, pin_hash, salt) VALUES (?, ?, ?, ?, ?)""", 5)
        {
          bindString(0, id)
          bindString(1, name)
          bindString(2, role)
          bindString(3, pin_hash)
          bindString(4, salt)
        }
    notifyQueries(496_737_617) { emit ->
      emit("user_profiles")
    }
  }

  public fun deleteUser(id: String) {
    driver.execute(1_295_161_155, """DELETE FROM user_profiles WHERE id = ?""", 1) {
          bindString(0, id)
        }
    notifyQueries(1_295_161_155) { emit ->
      emit("user_profiles")
    }
  }

  public fun enqueueSync(
    type: String,
    payload: String,
    created_at: Long,
  ) {
    driver.execute(-358_203_594,
        """INSERT INTO sync_queue (type, payload, created_at) VALUES (?, ?, ?)""", 3) {
          bindString(0, type)
          bindString(1, payload)
          bindLong(2, created_at)
        }
    notifyQueries(-358_203_594) { emit ->
      emit("sync_queue")
    }
  }

  public fun deleteSyncItems(id: Long) {
    driver.execute(1_302_398_317, """DELETE FROM sync_queue WHERE id = ?""", 1) {
          bindLong(0, id)
        }
    notifyQueries(1_302_398_317) { emit ->
      emit("sync_queue")
    }
  }

  public fun upsertMlScore(
    battery_index: Long,
    battery_index_: Long,
    soh_score: Double,
    rul_days: Long,
    anomaly_score: Double,
    r_int_trend: Double,
    timestamp: Long,
  ) {
    driver.execute(157_883_831, """
        |INSERT OR REPLACE INTO ml_scores (id, battery_index, soh_score, rul_days, anomaly_score, r_int_trend, timestamp)
        |VALUES (
        |    (SELECT id FROM ml_scores WHERE battery_index = ?),
        |    ?, ?, ?, ?, ?, ?
        |)
        """.trimMargin(), 7) {
          bindLong(0, battery_index)
          bindLong(1, battery_index_)
          bindDouble(2, soh_score)
          bindLong(3, rul_days)
          bindDouble(4, anomaly_score)
          bindDouble(5, r_int_trend)
          bindLong(6, timestamp)
        }
    notifyQueries(157_883_831) { emit ->
      emit("ml_scores")
    }
  }

  public fun purgeOldMlScores(timestamp: Long) {
    driver.execute(-1_174_721_111, """DELETE FROM ml_scores WHERE timestamp < ?""", 1) {
          bindLong(0, timestamp)
        }
    notifyQueries(-1_174_721_111) { emit ->
      emit("ml_scores")
    }
  }

  public fun upsertFleetHealth(
    fleet_health_score: Double,
    outlier_idx: Long,
    outlier_score: Double,
    imbalance_severity: Double,
    timestamp: Long,
  ) {
    driver.execute(-1_158_149_938, """
        |INSERT INTO fleet_health (fleet_health_score, outlier_idx, outlier_score, imbalance_severity, timestamp)
        |VALUES (?, ?, ?, ?, ?)
        """.trimMargin(), 5) {
          bindDouble(0, fleet_health_score)
          bindLong(1, outlier_idx)
          bindDouble(2, outlier_score)
          bindDouble(3, imbalance_severity)
          bindLong(4, timestamp)
        }
    notifyQueries(-1_158_149_938) { emit ->
      emit("fleet_health")
    }
  }

  public fun purgeOldFleetHealth(timestamp: Long) {
    driver.execute(-1_073_422_943, """DELETE FROM fleet_health WHERE timestamp < ?""", 1) {
          bindLong(0, timestamp)
        }
    notifyQueries(-1_073_422_943) { emit ->
      emit("fleet_health")
    }
  }

  public fun upsertDiagnostic(
    battery_index: Long,
    battery_index_: Long,
    diagnostic_text: String,
    severity: String,
    generated_at: Long,
  ) {
    driver.execute(1_277_758_371, """
        |INSERT OR REPLACE INTO diagnostics (id, battery_index, diagnostic_text, severity, generated_at)
        |VALUES (
        |    (SELECT id FROM diagnostics WHERE battery_index = ?),
        |    ?, ?, ?, ?
        |)
        """.trimMargin(), 5) {
          bindLong(0, battery_index)
          bindLong(1, battery_index_)
          bindString(2, diagnostic_text)
          bindString(3, severity)
          bindLong(4, generated_at)
        }
    notifyQueries(1_277_758_371) { emit ->
      emit("diagnostics")
    }
  }

  public fun purgeOldDiagnostics(generated_at: Long) {
    driver.execute(1_040_530_947, """DELETE FROM diagnostics WHERE generated_at < ?""", 1) {
          bindLong(0, generated_at)
        }
    notifyQueries(1_040_530_947) { emit ->
      emit("diagnostics")
    }
  }

  private inner class GetHistoryQuery<out T : Any>(
    public val battery_index: Long,
    public val timestamp: Long,
    mapper: (SqlCursor) -> T,
  ) : Query<T>(mapper) {
    override fun addListener(listener: Query.Listener) {
      driver.addListener("battery_history", listener = listener)
    }

    override fun removeListener(listener: Query.Listener) {
      driver.removeListener("battery_history", listener = listener)
    }

    override fun <R> execute(mapper: (SqlCursor) -> QueryResult<R>): QueryResult<R> =
        driver.executeQuery(1_494_327_179, """
    |SELECT battery_history.id, battery_history.timestamp, battery_history.battery_index, battery_history.voltage_mv, battery_history.current_ma, battery_history.state, battery_history.ah_discharge_mah, battery_history.ah_charge_mah FROM battery_history
    |WHERE battery_index = ? AND timestamp > ?
    |ORDER BY timestamp ASC
    """.trimMargin(), mapper, 2) {
      bindLong(0, battery_index)
      bindLong(1, timestamp)
    }

    override fun toString(): String = "BmuDatabase.sq:getHistory"
  }

  private inner class GetAuditEventsQuery<out T : Any>(
    public val `value`: Long,
    mapper: (SqlCursor) -> T,
  ) : Query<T>(mapper) {
    override fun addListener(listener: Query.Listener) {
      driver.addListener("audit_events", listener = listener)
    }

    override fun removeListener(listener: Query.Listener) {
      driver.removeListener("audit_events", listener = listener)
    }

    override fun <R> execute(mapper: (SqlCursor) -> QueryResult<R>): QueryResult<R> =
        driver.executeQuery(655_114_315, """
    |SELECT audit_events.id, audit_events.timestamp, audit_events.user_id, audit_events.action, audit_events.target, audit_events.detail, audit_events.synced FROM audit_events
    |ORDER BY timestamp DESC
    |LIMIT ?
    """.trimMargin(), mapper, 1) {
      bindLong(0, value)
    }

    override fun toString(): String = "BmuDatabase.sq:getAuditEvents"
  }

  private inner class GetAuditByActionQuery<out T : Any>(
    public val action: String,
    public val `value`: Long,
    mapper: (SqlCursor) -> T,
  ) : Query<T>(mapper) {
    override fun addListener(listener: Query.Listener) {
      driver.addListener("audit_events", listener = listener)
    }

    override fun removeListener(listener: Query.Listener) {
      driver.removeListener("audit_events", listener = listener)
    }

    override fun <R> execute(mapper: (SqlCursor) -> QueryResult<R>): QueryResult<R> =
        driver.executeQuery(-1_107_102_689, """
    |SELECT audit_events.id, audit_events.timestamp, audit_events.user_id, audit_events.action, audit_events.target, audit_events.detail, audit_events.synced FROM audit_events
    |WHERE action LIKE ?
    |ORDER BY timestamp DESC
    |LIMIT ?
    """.trimMargin(), mapper, 2) {
      bindString(0, action)
      bindLong(1, value)
    }

    override fun toString(): String = "BmuDatabase.sq:getAuditByAction"
  }

  private inner class GetAuditByTargetQuery<out T : Any>(
    public val target: Long?,
    public val `value`: Long,
    mapper: (SqlCursor) -> T,
  ) : Query<T>(mapper) {
    override fun addListener(listener: Query.Listener) {
      driver.addListener("audit_events", listener = listener)
    }

    override fun removeListener(listener: Query.Listener) {
      driver.removeListener("audit_events", listener = listener)
    }

    override fun <R> execute(mapper: (SqlCursor) -> QueryResult<R>): QueryResult<R> =
        driver.executeQuery(null, """
    |SELECT audit_events.id, audit_events.timestamp, audit_events.user_id, audit_events.action, audit_events.target, audit_events.detail, audit_events.synced FROM audit_events
    |WHERE target ${ if (target == null) "IS" else "=" } ?
    |ORDER BY timestamp DESC
    |LIMIT ?
    """.trimMargin(), mapper, 2) {
      bindLong(0, target)
      bindLong(1, value)
    }

    override fun toString(): String = "BmuDatabase.sq:getAuditByTarget"
  }

  private inner class GetAuditByActionAndTargetQuery<out T : Any>(
    public val action: String,
    public val target: Long?,
    public val `value`: Long,
    mapper: (SqlCursor) -> T,
  ) : Query<T>(mapper) {
    override fun addListener(listener: Query.Listener) {
      driver.addListener("audit_events", listener = listener)
    }

    override fun removeListener(listener: Query.Listener) {
      driver.removeListener("audit_events", listener = listener)
    }

    override fun <R> execute(mapper: (SqlCursor) -> QueryResult<R>): QueryResult<R> =
        driver.executeQuery(null, """
    |SELECT audit_events.id, audit_events.timestamp, audit_events.user_id, audit_events.action, audit_events.target, audit_events.detail, audit_events.synced FROM audit_events
    |WHERE action LIKE ? AND target ${ if (target == null) "IS" else "=" } ?
    |ORDER BY timestamp DESC
    |LIMIT ?
    """.trimMargin(), mapper, 3) {
      bindString(0, action)
      bindLong(1, target)
      bindLong(2, value)
    }

    override fun toString(): String = "BmuDatabase.sq:getAuditByActionAndTarget"
  }

  private inner class GetUnsyncedAuditQuery<out T : Any>(
    public val `value`: Long,
    mapper: (SqlCursor) -> T,
  ) : Query<T>(mapper) {
    override fun addListener(listener: Query.Listener) {
      driver.addListener("audit_events", listener = listener)
    }

    override fun removeListener(listener: Query.Listener) {
      driver.removeListener("audit_events", listener = listener)
    }

    override fun <R> execute(mapper: (SqlCursor) -> QueryResult<R>): QueryResult<R> =
        driver.executeQuery(715_909_375,
        """SELECT audit_events.id, audit_events.timestamp, audit_events.user_id, audit_events.action, audit_events.target, audit_events.detail, audit_events.synced FROM audit_events WHERE synced = 0 ORDER BY timestamp ASC LIMIT ?""",
        mapper, 1) {
      bindLong(0, value)
    }

    override fun toString(): String = "BmuDatabase.sq:getUnsyncedAudit"
  }

  private inner class GetUserByHashQuery<out T : Any>(
    public val pin_hash: String,
    mapper: (SqlCursor) -> T,
  ) : Query<T>(mapper) {
    override fun addListener(listener: Query.Listener) {
      driver.addListener("user_profiles", listener = listener)
    }

    override fun removeListener(listener: Query.Listener) {
      driver.removeListener("user_profiles", listener = listener)
    }

    override fun <R> execute(mapper: (SqlCursor) -> QueryResult<R>): QueryResult<R> =
        driver.executeQuery(-677_175_463,
        """SELECT user_profiles.id, user_profiles.name, user_profiles.role, user_profiles.pin_hash, user_profiles.salt FROM user_profiles WHERE pin_hash = ?""",
        mapper, 1) {
      bindString(0, pin_hash)
    }

    override fun toString(): String = "BmuDatabase.sq:getUserByHash"
  }

  private inner class DequeueSyncQuery<out T : Any>(
    public val `value`: Long,
    mapper: (SqlCursor) -> T,
  ) : Query<T>(mapper) {
    override fun addListener(listener: Query.Listener) {
      driver.addListener("sync_queue", listener = listener)
    }

    override fun removeListener(listener: Query.Listener) {
      driver.removeListener("sync_queue", listener = listener)
    }

    override fun <R> execute(mapper: (SqlCursor) -> QueryResult<R>): QueryResult<R> =
        driver.executeQuery(-1_087_597_986,
        """SELECT sync_queue.id, sync_queue.type, sync_queue.payload, sync_queue.created_at, sync_queue.retry_count FROM sync_queue ORDER BY created_at ASC LIMIT ?""",
        mapper, 1) {
      bindLong(0, value)
    }

    override fun toString(): String = "BmuDatabase.sq:dequeueSync"
  }

  private inner class GetMlScoreQuery<out T : Any>(
    public val battery_index: Long,
    mapper: (SqlCursor) -> T,
  ) : Query<T>(mapper) {
    override fun addListener(listener: Query.Listener) {
      driver.addListener("ml_scores", listener = listener)
    }

    override fun removeListener(listener: Query.Listener) {
      driver.removeListener("ml_scores", listener = listener)
    }

    override fun <R> execute(mapper: (SqlCursor) -> QueryResult<R>): QueryResult<R> =
        driver.executeQuery(1_692_706_602,
        """SELECT ml_scores.id, ml_scores.battery_index, ml_scores.soh_score, ml_scores.rul_days, ml_scores.anomaly_score, ml_scores.r_int_trend, ml_scores.timestamp FROM ml_scores WHERE battery_index = ?""",
        mapper, 1) {
      bindLong(0, battery_index)
    }

    override fun toString(): String = "BmuDatabase.sq:getMlScore"
  }

  private inner class GetDiagnosticQuery<out T : Any>(
    public val battery_index: Long,
    mapper: (SqlCursor) -> T,
  ) : Query<T>(mapper) {
    override fun addListener(listener: Query.Listener) {
      driver.addListener("diagnostics", listener = listener)
    }

    override fun removeListener(listener: Query.Listener) {
      driver.removeListener("diagnostics", listener = listener)
    }

    override fun <R> execute(mapper: (SqlCursor) -> QueryResult<R>): QueryResult<R> =
        driver.executeQuery(961_096_016,
        """SELECT diagnostics.id, diagnostics.battery_index, diagnostics.diagnostic_text, diagnostics.severity, diagnostics.generated_at FROM diagnostics WHERE battery_index = ? ORDER BY generated_at DESC LIMIT 1""",
        mapper, 1) {
      bindLong(0, battery_index)
    }

    override fun toString(): String = "BmuDatabase.sq:getDiagnostic"
  }
}
