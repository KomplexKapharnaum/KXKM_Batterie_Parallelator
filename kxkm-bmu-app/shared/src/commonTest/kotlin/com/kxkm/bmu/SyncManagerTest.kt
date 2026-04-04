package com.kxkm.bmu

import com.kxkm.bmu.db.PendingAuditSyncItem
import com.kxkm.bmu.model.AuditEvent
import com.kxkm.bmu.sync.SyncCloud
import com.kxkm.bmu.sync.SyncManager
import com.kxkm.bmu.sync.SyncStore
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals

private class FakeSyncStore(items: List<PendingAuditSyncItem>) : SyncStore {
    private val queue = items.toMutableList()
    val markedIds = mutableListOf<Long>()

    override fun countUnsyncedAudit(): Long = queue.size.toLong()

    override fun getUnsyncedAudit(limit: Int): List<PendingAuditSyncItem> =
        queue.take(limit)

    override fun markAuditSynced(ids: List<Long>) {
        markedIds.addAll(ids)
        queue.removeAll { it.id in ids }
    }
}

private class FakeSyncCloud(
    private val response: Boolean,
    private val throwError: Boolean = false
) : SyncCloud {
    var callCount = 0
    var lastPayload: String = ""

    override suspend fun syncBatch(payload: String): Boolean {
        callCount++
        lastPayload = payload
        if (throwError) error("network down")
        return response
    }
}

class SyncManagerTest {
    private fun event(id: Long): PendingAuditSyncItem {
        return PendingAuditSyncItem(
            id = id,
            event = AuditEvent(
                timestamp = 1_000L + id,
                userId = "u$id",
                action = "a$id",
                target = id.toInt(),
                detail = "d$id"
            )
        )
    }

    @Test
    fun runSyncCycleMarksEventsOnSuccess() = runTest {
        val store = FakeSyncStore(listOf(event(1), event(2)))
        val cloud = FakeSyncCloud(response = true)
        val manager = SyncManager(store, cloud)

        manager.runSyncCycle()

        assertEquals(1, cloud.callCount)
        assertEquals(listOf(1L, 2L), store.markedIds)
        assertEquals(1000L, manager.currentRetryDelayMs())
    }

    @Test
    fun runSyncCycleBackoffOnFailure() = runTest {
        val store = FakeSyncStore(listOf(event(1)))
        val cloud = FakeSyncCloud(response = false)
        val manager = SyncManager(store, cloud)

        manager.runSyncCycle()

        assertEquals(1, cloud.callCount)
        assertEquals(emptyList(), store.markedIds)
        assertEquals(2000L, manager.currentRetryDelayMs())
    }

    @Test
    fun runSyncCycleBackoffOnException() = runTest {
        val store = FakeSyncStore(listOf(event(1)))
        val cloud = FakeSyncCloud(response = true, throwError = true)
        val manager = SyncManager(store, cloud)

        manager.runSyncCycle()

        assertEquals(1, cloud.callCount)
        assertEquals(emptyList(), store.markedIds)
        assertEquals(2000L, manager.currentRetryDelayMs())
    }

    @Test
    fun runSyncCycleNoopWhenQueueEmpty() = runTest {
        val store = FakeSyncStore(emptyList())
        val cloud = FakeSyncCloud(response = true)
        val manager = SyncManager(store, cloud)

        manager.runSyncCycle()

        assertEquals(0, cloud.callCount)
        assertEquals(emptyList(), store.markedIds)
        assertEquals(1000L, manager.currentRetryDelayMs())
    }
}
