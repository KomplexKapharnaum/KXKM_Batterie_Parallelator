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
