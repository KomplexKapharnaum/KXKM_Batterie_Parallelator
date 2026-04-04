package com.kxkm.bmu.domain

import com.kxkm.bmu.db.DatabaseHelper
import com.kxkm.bmu.model.*
import com.kxkm.bmu.transport.SohRestClient
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.datetime.Clock

/**
 * Aggregates BLE real-time SOH (from edge model) with cloud ML scores and diagnostics.
 * Fetches from REST every 30 min, caches in SQLDelight, serves from cache when offline.
 */
class SohUseCase(
    private val restClient: SohRestClient,
    private val db: DatabaseHelper
) {
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    private val _mlScores = MutableStateFlow<List<MlScore>>(emptyList())
    val mlScores: StateFlow<List<MlScore>> = _mlScores.asStateFlow()

    private val _fleetHealth = MutableStateFlow<FleetHealth?>(null)
    val fleetHealth: StateFlow<FleetHealth?> = _fleetHealth.asStateFlow()

    private val _lastRefreshMs = MutableStateFlow(0L)
    val lastRefreshMs: StateFlow<Long> = _lastRefreshMs.asStateFlow()

    private val _isRefreshing = MutableStateFlow(false)
    val isRefreshing: StateFlow<Boolean> = _isRefreshing.asStateFlow()

    companion object {
        private const val REFRESH_INTERVAL_MS = 30 * 60 * 1000L // 30 min
        private const val ANOMALY_THRESHOLD = 0.7f
        private const val SOH_CRITICAL_THRESHOLD = 70
    }

    /** Load cached data and start periodic refresh */
    fun start() {
        // Load from cache immediately
        scope.launch {
            _mlScores.value = db.getLatestMlScores()
            _fleetHealth.value = db.getLatestFleetHealth()
        }
        // Periodic refresh from REST
        scope.launch {
            while (true) {
                refreshFromCloud()
                delay(REFRESH_INTERVAL_MS)
            }
        }
    }

    /** Force refresh from cloud (user-triggered) */
    suspend fun refreshFromCloud() {
        _isRefreshing.value = true
        try {
            // Fetch ML scores
            val scores = restClient.getAllScores()
            scores.forEach { db.upsertMlScore(it) }
            _mlScores.value = scores

            // Fetch fleet health
            val fleet = restClient.getFleetHealth()
            db.upsertFleetHealth(fleet)
            _fleetHealth.value = fleet

            _lastRefreshMs.value = Clock.System.now().toEpochMilliseconds()
        } catch (_: Exception) {
            // Offline — keep cached data, do not overwrite
        } finally {
            _isRefreshing.value = false
        }
    }

    /** Get SOH history for trend chart */
    suspend fun getSohHistory(batteryIndex: Int, days: Int = 7): List<MlScore> {
        return try {
            restClient.getSohHistory(batteryIndex, days)
        } catch (_: Exception) {
            emptyList() // No cache for history — requires connectivity
        }
    }

    /** Get cached diagnostic for a battery */
    fun getCachedDiagnostic(batteryIndex: Int): Diagnostic? {
        return db.getDiagnostic(batteryIndex)
    }

    /** Fetch diagnostic from cloud (cached daily, or on-demand refresh) */
    suspend fun getDiagnostic(batteryIndex: Int, forceRefresh: Boolean = false): Diagnostic? {
        return try {
            val diag = if (forceRefresh) {
                restClient.refreshDiagnostic(batteryIndex)
            } else {
                restClient.getDiagnostic(batteryIndex)
            }
            db.upsertDiagnostic(diag)
            diag
        } catch (_: Exception) {
            db.getDiagnostic(batteryIndex) // Fallback to cache
        }
    }

    /** Check for anomalies requiring notification */
    fun getAnomalyAlerts(): List<MlScore> {
        return _mlScores.value.filter { score ->
            score.anomalyScore > ANOMALY_THRESHOLD ||
            (score.sohScore * 100).toInt() < SOH_CRITICAL_THRESHOLD
        }
    }

    /** Callback-based APIs for iOS/Android ViewModels */
    fun observeMlScores(callback: (List<MlScore>) -> Unit) {
        scope.launch { mlScores.collect { callback(it) } }
    }

    fun observeFleetHealth(callback: (FleetHealth?) -> Unit) {
        scope.launch { fleetHealth.collect { callback(it) } }
    }

    fun close() {
        scope.cancel()
    }
}
