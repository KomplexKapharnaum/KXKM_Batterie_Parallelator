package com.kxkm.bmu.sync

import com.kxkm.bmu.db.DatabaseHelper
import com.kxkm.bmu.db.PendingAuditSyncItem
import com.kxkm.bmu.transport.CloudRestClient
import kotlinx.coroutines.*
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

class SyncManager(
    private val store: SyncStore,
    private val cloud: SyncCloud?
) {
    constructor(
        db: DatabaseHelper,
        restClient: CloudRestClient?
    ) : this(
        store = DatabaseSyncStore(db),
        cloud = restClient?.let { CloudRestSync(it) }
    )

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private val json = Json { ignoreUnknownKeys = true }
    private var retryDelayMs = 1000L

    fun start() {
        if (cloud == null) return
        scope.launch {
            while (true) {
                runSyncCycle()
                delay(retryDelayMs)
            }
        }
    }

    suspend fun runSyncCycle() {
        val client = cloud ?: return
        try {
            val pending = store.countUnsyncedAudit()
            if (pending <= 0) {
                retryDelayMs = 1000L
                return
            }

            val unsynced = store.getUnsyncedAudit(limit = 100)
            val payload = json.encodeToString(unsynced.map { it.event })
            val success = client.syncBatch(payload)
            if (success) {
                store.markAuditSynced(unsynced.map { it.id })
                retryDelayMs = if (store.countUnsyncedAudit() == 0L) 1000L else retryDelayMs
            } else {
                retryDelayMs = (retryDelayMs * 2).coerceAtMost(60_000L)
            }
        } catch (_: Exception) {
            retryDelayMs = (retryDelayMs * 2).coerceAtMost(60_000L)
        }
    }

    fun currentRetryDelayMs(): Long = retryDelayMs

    fun close() {
        scope.cancel()
    }
}

interface SyncStore {
    fun countUnsyncedAudit(): Long
    fun getUnsyncedAudit(limit: Int): List<PendingAuditSyncItem>
    fun markAuditSynced(ids: List<Long>)
}

interface SyncCloud {
    suspend fun syncBatch(payload: String): Boolean
}

private class DatabaseSyncStore(private val db: DatabaseHelper) : SyncStore {
    override fun countUnsyncedAudit(): Long = db.countUnsyncedAudit()
    override fun getUnsyncedAudit(limit: Int): List<PendingAuditSyncItem> = db.getUnsyncedAudit(limit)
    override fun markAuditSynced(ids: List<Long>) = db.markAuditSynced(ids)
}

private class CloudRestSync(private val rest: CloudRestClient) : SyncCloud {
    override suspend fun syncBatch(payload: String): Boolean = rest.syncBatch(payload)
}
