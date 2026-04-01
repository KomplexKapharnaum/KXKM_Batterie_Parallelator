package com.kxkm.bmu.sync

import com.kxkm.bmu.db.DatabaseHelper
import com.kxkm.bmu.transport.CloudRestClient
import kotlinx.coroutines.*
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

class SyncManager(
    private val db: DatabaseHelper,
    private val restClient: CloudRestClient?
) {
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private val json = Json { ignoreUnknownKeys = true }
    private var retryDelayMs = 1000L

    fun start() {
        if (restClient == null) return
        scope.launch {
            while (true) {
                try {
                    val pending = db.countPendingSync()
                    if (pending > 0) {
                        val events = db.getAuditEvents(limit = 100)
                        val payload = json.encodeToString(events)
                        val success = restClient.syncBatch(payload)
                        if (success) {
                            retryDelayMs = 1000L // reset
                        } else {
                            retryDelayMs = (retryDelayMs * 2).coerceAtMost(60_000L)
                        }
                    }
                } catch (_: Exception) {
                    retryDelayMs = (retryDelayMs * 2).coerceAtMost(60_000L)
                }
                delay(retryDelayMs)
            }
        }
    }
}
