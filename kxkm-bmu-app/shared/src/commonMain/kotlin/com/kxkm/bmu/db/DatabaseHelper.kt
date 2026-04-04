package com.kxkm.bmu.db

import com.kxkm.bmu.model.*
import kotlinx.datetime.Clock

class DatabaseHelper(driverFactory: DriverFactory) {
    private val database = BmuDatabase(driverFactory.createDriver())
    private val queries = database.bmuDatabaseQueries

    // -- History --
    fun insertHistory(battery: BatteryState) {
        queries.insertHistory(
            timestamp = Clock.System.now().toEpochMilliseconds(),
            battery_index = battery.index.toLong(),
            voltage_mv = battery.voltageMv.toLong(),
            current_ma = battery.currentMa.toLong(),
            state = battery.state.name,
            ah_discharge_mah = battery.ahDischargeMah.toLong(),
            ah_charge_mah = battery.ahChargeMah.toLong()
        )
    }

    fun getHistory(batteryIndex: Int, sinceMs: Long): List<BatteryHistoryPoint> {
        return queries.getHistory(batteryIndex.toLong(), sinceMs).executeAsList().map {
            BatteryHistoryPoint(
                timestamp = it.timestamp,
                voltageMv = it.voltage_mv.toInt(),
                currentMa = it.current_ma.toInt()
            )
        }
    }

    fun getLastKnownBatteries(): List<BatteryState> {
        return queries.getLastKnownBatteries().executeAsList().map {
            BatteryState(
                index = it.battery_index.toInt(),
                voltageMv = it.voltage_mv.toInt(),
                currentMa = it.current_ma.toInt(),
                state = runCatching { BatteryStatus.valueOf(it.state) }.getOrDefault(BatteryStatus.ERROR),
                ahDischargeMah = it.ah_discharge_mah.toInt(),
                ahChargeMah = it.ah_charge_mah.toInt(),
                nbSwitch = 0
            )
        }
    }

    fun purgeOldHistory(olderThanMs: Long) {
        queries.purgeOldHistory(olderThanMs)
    }

    // -- Audit --
    fun insertAudit(event: AuditEvent) {
        queries.insertAudit(
            timestamp = event.timestamp,
            user_id = event.userId,
            action = event.action,
            target = event.target?.toLong(),
            detail = event.detail
        )
    }

    fun getAuditEvents(limit: Int = 200): List<AuditEvent> {
        return queries.getAuditEvents(limit.toLong()).executeAsList().map { it.toAuditEvent() }
    }

    fun getAuditFiltered(action: String?, batteryIndex: Int?, limit: Int = 200): List<AuditEvent> {
        val actionFilter = action?.let { "%$it%" }
        return queries.getAuditFiltered(
            actionFilter, actionFilter,
            batteryIndex?.toLong(), batteryIndex?.toLong(),
            limit.toLong()
        ).executeAsList().map { it.toAuditEvent() }
    }

    fun getUnsyncedAudit(limit: Int = 100): List<PendingAuditSyncItem> {
        return queries.getUnsyncedAudit(limit.toLong()).executeAsList().map {
            PendingAuditSyncItem(
                id = it.id,
                event = AuditEvent(
                    timestamp = it.timestamp,
                    userId = it.user_id,
                    action = it.action,
                    target = it.target?.toInt(),
                    detail = it.detail
                )
            )
        }
    }

    fun markAuditSynced(ids: List<Long>) {
        if (ids.isEmpty()) return
        queries.markAuditSynced(ids)
    }

    fun countUnsyncedAudit(): Long = queries.countUnsyncedAudit().executeAsOne()

    // -- Users --
    fun insertUser(user: UserProfile) {
        queries.insertUser(user.id, user.name, user.role.name, user.pinHash, user.salt)
    }

    fun getAllUsers(): List<UserProfile> {
        return queries.getAllUsers().executeAsList().map {
            UserProfile(it.id, it.name, UserRole.valueOf(it.role), it.pin_hash, it.salt)
        }
    }

    fun findUserByHash(pinHash: String): UserProfile? {
        return queries.getUserByHash(pinHash).executeAsOneOrNull()?.let {
            UserProfile(it.id, it.name, UserRole.valueOf(it.role), it.pin_hash, it.salt)
        }
    }

    fun deleteUser(userId: String) = queries.deleteUser(userId)
    fun countUsers(): Long = queries.countUsers().executeAsOne()

    // -- Sync queue --
    fun countPendingSync(): Long = queries.countPendingSync().executeAsOne()

    // -- ML Scores cache --
    fun upsertMlScore(score: MlScore) {
        queries.upsertMlScore(
            battery_index = score.battery.toLong(),
            battery_index_ = score.battery.toLong(),
            soh_score = score.sohScore.toDouble(),
            rul_days = score.rulDays.toLong(),
            anomaly_score = score.anomalyScore.toDouble(),
            r_int_trend = score.rIntTrendMohmPerDay.toDouble(),
            timestamp = score.timestamp
        )
    }

    fun getLatestMlScores(): List<MlScore> {
        return queries.getLatestMlScores().executeAsList().map {
            MlScore(
                battery = it.battery_index.toInt(),
                sohScore = it.soh_score.toFloat(),
                rulDays = it.rul_days.toInt(),
                anomalyScore = it.anomaly_score.toFloat(),
                rIntTrendMohmPerDay = it.r_int_trend.toFloat(),
                timestamp = it.timestamp
            )
        }
    }

    fun getMlScore(batteryIndex: Int): MlScore? {
        return queries.getMlScore(batteryIndex.toLong()).executeAsOneOrNull()?.let {
            MlScore(
                battery = it.battery_index.toInt(),
                sohScore = it.soh_score.toFloat(),
                rulDays = it.rul_days.toInt(),
                anomalyScore = it.anomaly_score.toFloat(),
                rIntTrendMohmPerDay = it.r_int_trend.toFloat(),
                timestamp = it.timestamp
            )
        }
    }

    // -- Fleet Health cache --
    fun upsertFleetHealth(fleet: FleetHealth) {
        queries.upsertFleetHealth(
            fleet_health_score = fleet.fleetHealth.toDouble(),
            outlier_idx = fleet.outlierIdx.toLong(),
            outlier_score = fleet.outlierScore.toDouble(),
            imbalance_severity = fleet.imbalanceSeverity.toDouble(),
            timestamp = fleet.timestamp
        )
    }

    fun getLatestFleetHealth(): FleetHealth? {
        return queries.getLatestFleetHealth().executeAsOneOrNull()?.let {
            FleetHealth(
                fleetHealth = it.fleet_health_score.toFloat(),
                outlierIdx = it.outlier_idx.toInt(),
                outlierScore = it.outlier_score.toFloat(),
                imbalanceSeverity = it.imbalance_severity.toFloat(),
                timestamp = it.timestamp
            )
        }
    }

    // -- Diagnostics cache --
    fun upsertDiagnostic(diag: Diagnostic) {
        queries.upsertDiagnostic(
            battery_index = diag.battery.toLong(),
            battery_index_ = diag.battery.toLong(),
            diagnostic_text = diag.diagnostic,
            severity = diag.severity.name,
            generated_at = diag.generatedAt
        )
    }

    fun getDiagnostic(batteryIndex: Int): Diagnostic? {
        return queries.getDiagnostic(batteryIndex.toLong()).executeAsOneOrNull()?.let {
            Diagnostic(
                battery = it.battery_index.toInt(),
                diagnostic = it.diagnostic_text,
                severity = runCatching { DiagnosticSeverity.valueOf(it.severity) }
                    .getOrDefault(DiagnosticSeverity.info),
                generatedAt = it.generated_at
            )
        }
    }

    fun getAllDiagnostics(): List<Diagnostic> {
        return queries.getAllDiagnostics().executeAsList().map {
            Diagnostic(
                battery = it.battery_index.toInt(),
                diagnostic = it.diagnostic_text,
                severity = runCatching { DiagnosticSeverity.valueOf(it.severity) }
                    .getOrDefault(DiagnosticSeverity.info),
                generatedAt = it.generated_at
            )
        }
    }
}

data class BatteryHistoryPoint(
    val timestamp: Long,
    val voltageMv: Int,
    val currentMa: Int
)

data class PendingAuditSyncItem(
    val id: Long,
    val event: AuditEvent
)

private fun Audit_events.toAuditEvent() = AuditEvent(
    timestamp = timestamp,
    userId = user_id,
    action = action,
    target = target?.toInt(),
    detail = detail
)
