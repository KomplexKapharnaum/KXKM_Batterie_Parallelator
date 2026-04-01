package com.kxkm.bmu.sync

import com.kxkm.bmu.db.DatabaseHelper
import com.kxkm.bmu.model.AuditEvent
import kotlinx.datetime.Clock

class AuditUseCase(private val db: DatabaseHelper) {
    fun record(userId: String, action: String, target: Int? = null, detail: String? = null) {
        db.insertAudit(AuditEvent(
            timestamp = Clock.System.now().toEpochMilliseconds(),
            userId = userId,
            action = action,
            target = target,
            detail = detail
        ))
    }

    fun getEvents(action: String? = null, batteryIndex: Int? = null): List<AuditEvent> {
        return if (action == null && batteryIndex == null) {
            db.getAuditEvents()
        } else {
            db.getAuditFiltered(action, batteryIndex)
        }
    }

    fun getPendingSyncCount(): Long = db.countUnsyncedAudit()
}
