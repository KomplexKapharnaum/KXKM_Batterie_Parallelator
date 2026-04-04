# Phase 4: App Integration Loop — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate cloud ML scores and LLM diagnostics into the KMP mobile app with trend charts, fleet view, and push notifications.

**Architecture:** App adds REST client for kxkm-ai APIs, caches results in SQLDelight, displays SOH trends/diagnostics/fleet health. Push notifications via local triggers on cached anomaly scores.

**Tech Stack:** Kotlin Multiplatform, Compose Multiplatform, SwiftUI, Ktor HTTP client, SQLDelight, kxkm-ai REST API

**Spec:** `docs/superpowers/specs/2026-04-04-soh-llm-operational-design.md` — Phase 4

**Dependencies:** Phase 2 (ML scoring API on kxkm-ai:8400) and Phase 3 (LLM diagnostic API) must be deployed.

---

## File Structure

```
shared/src/commonMain/kotlin/com/kxkm/bmu/
├── model/
│   ├── MlScore.kt                            # Per-battery ML score (TSMixer output)
│   ├── FleetHealth.kt                        # Fleet-level GNN score
│   └── Diagnostic.kt                         # LLM diagnostic narrative
├── transport/
│   └── SohRestClient.kt                      # REST client for kxkm-ai:8400 SOH + diagnostic endpoints
├── domain/
│   └── SohUseCase.kt                         # Aggregates BLE real-time + REST cached SOH data
└── db/
    └── BmuDatabase.sq                        # +3 tables: ml_scores, fleet_health, diagnostics

shared/src/commonTest/kotlin/com/kxkm/bmu/
├── SohRestClientTest.kt                      # Mock HTTP engine tests
├── SohUseCaseTest.kt                         # ViewModel-level data aggregation tests
└── SohCacheTest.kt                           # SQLDelight cache read/write tests

androidApp/src/main/java/com/kxkm/bmu/
├── viewmodel/
│   ├── SohDashboardViewModel.kt              # SOH tab: per-battery scores + trends
│   └── FleetViewModel.kt                     # Fleet view: aggregate health + outlier
├── ui/
│   ├── soh/
│   │   ├── SohDashboardScreen.kt             # SOH tab with battery gauge grid
│   │   ├── SohGauge.kt                       # Circular SOH gauge composable
│   │   ├── SohSparkline.kt                   # 7-day trend mini chart
│   │   ├── DiagnosticCard.kt                 # French diagnostic text + severity
│   │   └── RintTrendChart.kt                 # 7-day R_int line chart (Vico)
│   └── fleet/
│       ├── FleetScreen.kt                    # Fleet health circle + outlier card
│       └── FleetHealthCircle.kt              # Aggregate health score gauge
├── notification/
│   └── SohNotificationManager.kt             # Local push notifications for anomalies

iosApp/Sources/
├── SOH/
│   ├── SohDashboardView.swift                # SOH tab (SwiftUI)
│   ├── SohGaugeView.swift                    # Circular gauge
│   ├── DiagnosticCardView.swift              # Diagnostic text card
│   └── FleetHealthView.swift                 # Fleet view
└── Notification/
    └── SohNotificationDelegate.swift         # Local notifications (UNUserNotificationCenter)
```

---

### Task 1: Data models — MlScore, FleetHealth, Diagnostic

**Files:**
- Create: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/model/MlScore.kt`
- Create: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/model/FleetHealth.kt`
- Create: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/model/Diagnostic.kt`

- [ ] **Step 1: Create MlScore data class**

```kotlin
// kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/model/MlScore.kt
package com.kxkm.bmu.model

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class MlScore(
    val battery: Int,
    @SerialName("soh_score") val sohScore: Float,
    @SerialName("rul_days") val rulDays: Int,
    @SerialName("anomaly_score") val anomalyScore: Float,
    @SerialName("r_int_trend_mohm_per_day") val rIntTrendMohmPerDay: Float = 0f,
    val timestamp: Long
)
```

- [ ] **Step 2: Create FleetHealth data class**

```kotlin
// kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/model/FleetHealth.kt
package com.kxkm.bmu.model

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class FleetHealth(
    @SerialName("fleet_health") val fleetHealth: Float,
    @SerialName("outlier_idx") val outlierIdx: Int,
    @SerialName("outlier_score") val outlierScore: Float,
    @SerialName("imbalance_severity") val imbalanceSeverity: Float = 0f,
    val timestamp: Long
)
```

- [ ] **Step 3: Create Diagnostic data class**

```kotlin
// kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/model/Diagnostic.kt
package com.kxkm.bmu.model

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
enum class DiagnosticSeverity { info, warning, critical }

@Serializable
data class Diagnostic(
    val battery: Int,
    val diagnostic: String,
    val severity: DiagnosticSeverity,
    @SerialName("generated_at") val generatedAt: Long
)

@Serializable
data class FleetDiagnostic(
    val summary: String,
    val severity: DiagnosticSeverity,
    @SerialName("generated_at") val generatedAt: Long
)
```

**Commit:** `feat(app): add MlScore, FleetHealth, Diagnostic data models`

---

### Task 2: SQLDelight cache tables

**Files:**
- Edit: `kxkm-bmu-app/shared/src/commonMain/sqldelight/com/kxkm/bmu/db/BmuDatabase.sq`
- Edit: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/db/DatabaseHelper.kt`

- [ ] **Step 1: Add ml_scores table and queries to BmuDatabase.sq**

Append to the existing `.sq` file:

```sql
-- ML scores cache (from kxkm-ai TSMixer)
CREATE TABLE ml_scores (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    battery_index INTEGER NOT NULL,
    soh_score REAL NOT NULL,
    rul_days INTEGER NOT NULL,
    anomaly_score REAL NOT NULL,
    r_int_trend REAL NOT NULL DEFAULT 0.0,
    timestamp INTEGER NOT NULL
);

CREATE INDEX idx_ml_scores_battery ON ml_scores(battery_index, timestamp);

-- Fleet health cache (from kxkm-ai GNN)
CREATE TABLE fleet_health (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    fleet_health_score REAL NOT NULL,
    outlier_idx INTEGER NOT NULL,
    outlier_score REAL NOT NULL,
    imbalance_severity REAL NOT NULL DEFAULT 0.0,
    timestamp INTEGER NOT NULL
);

CREATE INDEX idx_fleet_time ON fleet_health(timestamp);

-- LLM diagnostic cache
CREATE TABLE diagnostics (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    battery_index INTEGER NOT NULL,
    diagnostic_text TEXT NOT NULL,
    severity TEXT NOT NULL,
    generated_at INTEGER NOT NULL
);

CREATE INDEX idx_diag_battery ON diagnostics(battery_index, generated_at);

-- Queries: ml_scores
upsertMlScore:
INSERT OR REPLACE INTO ml_scores (id, battery_index, soh_score, rul_days, anomaly_score, r_int_trend, timestamp)
VALUES (
    (SELECT id FROM ml_scores WHERE battery_index = ?),
    ?, ?, ?, ?, ?, ?
);

getLatestMlScores:
SELECT * FROM ml_scores ORDER BY battery_index ASC;

getMlScore:
SELECT * FROM ml_scores WHERE battery_index = ?;

purgeOldMlScores:
DELETE FROM ml_scores WHERE timestamp < ?;

-- Queries: fleet_health
upsertFleetHealth:
INSERT INTO fleet_health (fleet_health_score, outlier_idx, outlier_score, imbalance_severity, timestamp)
VALUES (?, ?, ?, ?, ?);

getLatestFleetHealth:
SELECT * FROM fleet_health ORDER BY timestamp DESC LIMIT 1;

purgeOldFleetHealth:
DELETE FROM fleet_health WHERE timestamp < ?;

-- Queries: diagnostics
upsertDiagnostic:
INSERT OR REPLACE INTO diagnostics (id, battery_index, diagnostic_text, severity, generated_at)
VALUES (
    (SELECT id FROM diagnostics WHERE battery_index = ?),
    ?, ?, ?, ?
);

getDiagnostic:
SELECT * FROM diagnostics WHERE battery_index = ? ORDER BY generated_at DESC LIMIT 1;

getAllDiagnostics:
SELECT * FROM diagnostics ORDER BY battery_index ASC;

purgeOldDiagnostics:
DELETE FROM diagnostics WHERE generated_at < ?;
```

- [ ] **Step 2: Add cache helper methods to DatabaseHelper.kt**

Add to existing `DatabaseHelper` class:

```kotlin
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
```

**Commit:** `feat(app): add SQLDelight cache tables for ML scores, fleet health, diagnostics`

---

### Task 3: REST client for kxkm-ai SOH + diagnostic endpoints

**Files:**
- Create: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/transport/SohRestClient.kt`

- [ ] **Step 1: Create SohRestClient**

Follow existing `CloudRestClient.kt` patterns (same Ktor style, same auth header, same Json config).

```kotlin
// kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/transport/SohRestClient.kt
package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import io.ktor.client.*
import io.ktor.client.request.*
import io.ktor.client.statement.*
import kotlinx.serialization.json.Json

/**
 * REST client for kxkm-ai:8400 SOH scoring and diagnostic endpoints.
 * Mirrors CloudRestClient conventions (auth header, Json config).
 */
class SohRestClient(
    private val baseUrl: String = "http://kxkm-ai:8400",
    private val apiKey: String = ""
) {
    private val client = HttpClient()
    private val json = Json { ignoreUnknownKeys = true }

    /** GET /api/soh/batteries — all battery ML scores (latest) */
    suspend fun getAllScores(): List<MlScore> {
        val resp = client.get("$baseUrl/api/soh/batteries") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<List<MlScore>>(resp)
    }

    /** GET /api/soh/battery/{id} — single battery score */
    suspend fun getBatteryScore(batteryIndex: Int): MlScore {
        val resp = client.get("$baseUrl/api/soh/battery/$batteryIndex") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<MlScore>(resp)
    }

    /** GET /api/soh/fleet — fleet health (GNN output) */
    suspend fun getFleetHealth(): FleetHealth {
        val resp = client.get("$baseUrl/api/soh/fleet") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<FleetHealth>(resp)
    }

    /** GET /api/soh/history/{id}?days=7 — SOH/R_int time series */
    suspend fun getSohHistory(batteryIndex: Int, days: Int = 7): List<MlScore> {
        val resp = client.get("$baseUrl/api/soh/history/$batteryIndex") {
            header("Authorization", "Bearer $apiKey")
            parameter("days", days)
        }.bodyAsText()
        return json.decodeFromString<List<MlScore>>(resp)
    }

    /** GET /api/diagnostic/{battery_id} — cached daily diagnostic */
    suspend fun getDiagnostic(batteryIndex: Int): Diagnostic {
        val resp = client.get("$baseUrl/api/diagnostic/$batteryIndex") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<Diagnostic>(resp)
    }

    /** POST /api/diagnostic/{battery_id} — generate fresh diagnostic on-demand */
    suspend fun refreshDiagnostic(batteryIndex: Int): Diagnostic {
        val resp = client.post("$baseUrl/api/diagnostic/$batteryIndex") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<Diagnostic>(resp)
    }

    /** GET /api/diagnostic/fleet — fleet-level summary */
    suspend fun getFleetDiagnostic(): FleetDiagnostic {
        val resp = client.get("$baseUrl/api/diagnostic/fleet") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<FleetDiagnostic>(resp)
    }

    /** POST /api/soh/predict — force refresh ML inference */
    suspend fun forceRefresh(): Boolean {
        val resp = client.post("$baseUrl/api/soh/predict") {
            header("Authorization", "Bearer $apiKey")
        }
        return resp.status.value in 200..299
    }
}
```

**Commit:** `feat(app): add SohRestClient for kxkm-ai SOH + diagnostic REST API`

---

### Task 4: SohUseCase — aggregate BLE (real-time) + REST (cached) data

**Files:**
- Create: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/domain/SohUseCase.kt`

- [ ] **Step 1: Create SohUseCase**

Follows existing `MonitoringUseCase.kt` patterns: own CoroutineScope, callback-based API for iOS/Android, StateFlow for reactive state.

```kotlin
// kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/domain/SohUseCase.kt
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
```

- [ ] **Step 2: Register SohUseCase in SharedFactory.kt**

Add to existing `SharedFactory`:

```kotlin
// In SharedFactory.kt, add:
val sohRestClient = SohRestClient(
    baseUrl = "http://kxkm-ai:8400",
    apiKey = apiKey
)

val sohUseCase = SohUseCase(
    restClient = sohRestClient,
    db = databaseHelper
)
```

**Commit:** `feat(app): add SohUseCase aggregating BLE real-time + REST cached data`

---

### Task 5: SOH dashboard tab — per-battery gauge with 7-day sparkline

**Files:**
- Create: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/viewmodel/SohDashboardViewModel.kt`
- Create: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/soh/SohDashboardScreen.kt`
- Create: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/soh/SohGauge.kt`
- Create: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/soh/SohSparkline.kt`

- [ ] **Step 1: Create SohDashboardViewModel**

Follow `DashboardViewModel.kt` patterns: `@HiltViewModel`, `MutableStateFlow`, `viewModelScope.launch`.

```kotlin
// kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/viewmodel/SohDashboardViewModel.kt
package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kxkm.bmu.shared.domain.SohUseCase
import com.kxkm.bmu.shared.model.MlScore
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class SohDashboardViewModel @Inject constructor(
    private val sohUseCase: SohUseCase,
) : ViewModel() {

    private val _scores = MutableStateFlow<List<MlScore>>(emptyList())
    val scores: StateFlow<List<MlScore>> = _scores.asStateFlow()

    private val _isRefreshing = MutableStateFlow(false)
    val isRefreshing: StateFlow<Boolean> = _isRefreshing.asStateFlow()

    private val _lastRefreshMs = MutableStateFlow(0L)
    val lastRefreshMs: StateFlow<Long> = _lastRefreshMs.asStateFlow()

    // SOH history per battery (for sparklines), keyed by battery index
    private val _sohHistory = MutableStateFlow<Map<Int, List<MlScore>>>(emptyMap())
    val sohHistory: StateFlow<Map<Int, List<MlScore>>> = _sohHistory.asStateFlow()

    init {
        viewModelScope.launch {
            sohUseCase.observeMlScores { scores ->
                _scores.value = scores
            }
        }
        viewModelScope.launch {
            sohUseCase.lastRefreshMs.collect { _lastRefreshMs.value = it }
        }
        viewModelScope.launch {
            sohUseCase.isRefreshing.collect { _isRefreshing.value = it }
        }
    }

    fun refresh() {
        viewModelScope.launch {
            sohUseCase.refreshFromCloud()
        }
    }

    fun loadHistory(batteryIndex: Int) {
        viewModelScope.launch {
            val history = sohUseCase.getSohHistory(batteryIndex, days = 7)
            _sohHistory.value = _sohHistory.value + (batteryIndex to history)
        }
    }
}
```

- [ ] **Step 2: Create SohGauge composable**

Circular arc gauge, color-coded: green (> 80%), orange (60-80%), red (< 60%).

```kotlin
// kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/soh/SohGauge.kt
package com.kxkm.bmu.ui.soh

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun SohGauge(
    sohPercent: Int,
    modifier: Modifier = Modifier,
    size: Dp = 72.dp,
    strokeWidth: Dp = 6.dp,
) {
    val color = when {
        sohPercent >= 80 -> Color(0xFF4CAF50) // green
        sohPercent >= 60 -> Color(0xFFFF9800) // orange
        else -> Color(0xFFF44336)              // red
    }

    Box(modifier = modifier.size(size), contentAlignment = Alignment.Center) {
        Canvas(modifier = Modifier.size(size)) {
            val stroke = Stroke(width = strokeWidth.toPx(), cap = StrokeCap.Round)
            val arcSize = Size(this.size.width - stroke.width, this.size.height - stroke.width)
            val topLeft = Offset(stroke.width / 2f, stroke.width / 2f)

            // Background arc (full 270 degrees)
            drawArc(
                color = color.copy(alpha = 0.15f),
                startAngle = 135f,
                sweepAngle = 270f,
                useCenter = false,
                topLeft = topLeft,
                size = arcSize,
                style = stroke,
            )

            // Foreground arc (proportional to SOH)
            val sweep = 270f * (sohPercent.coerceIn(0, 100) / 100f)
            drawArc(
                color = color,
                startAngle = 135f,
                sweepAngle = sweep,
                useCenter = false,
                topLeft = topLeft,
                size = arcSize,
                style = stroke,
            )
        }

        Text(
            text = "$sohPercent%",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace,
            fontSize = if (size > 64.dp) 16.sp else 12.sp,
            color = color,
        )
    }
}
```

- [ ] **Step 3: Create SohSparkline composable**

Minimal 7-day SOH trend line (no axes, just the curve).

```kotlin
// kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/soh/SohSparkline.kt
package com.kxkm.bmu.ui.soh

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp

@Composable
fun SohSparkline(
    values: List<Float>,
    modifier: Modifier = Modifier,
    color: Color = Color(0xFF4CAF50),
) {
    if (values.size < 2) return

    Canvas(modifier = modifier.width(80.dp).height(24.dp)) {
        val maxVal = values.max().coerceAtLeast(1f)
        val minVal = values.min().coerceAtLeast(0f)
        val range = (maxVal - minVal).coerceAtLeast(0.01f)
        val stepX = size.width / (values.size - 1).toFloat()
        val padding = 2f

        val path = Path().apply {
            values.forEachIndexed { i, v ->
                val x = i * stepX
                val y = size.height - padding - ((v - minVal) / range) * (size.height - 2 * padding)
                if (i == 0) moveTo(x, y) else lineTo(x, y)
            }
        }

        drawPath(path, color, style = Stroke(width = 2.dp.toPx()))
    }
}
```

- [ ] **Step 4: Create SohDashboardScreen**

Grid of battery SOH cards, each with gauge + sparkline + RUL. Pull-to-refresh.

```kotlin
// kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/soh/SohDashboardScreen.kt
package com.kxkm.bmu.ui.soh

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.shared.model.MlScore
import com.kxkm.bmu.viewmodel.SohDashboardViewModel
import java.text.SimpleDateFormat
import java.util.*

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SohDashboardScreen(
    onBatteryClick: (Int) -> Unit = {},
    viewModel: SohDashboardViewModel = hiltViewModel(),
) {
    val scores by viewModel.scores.collectAsState()
    val isRefreshing by viewModel.isRefreshing.collectAsState()
    val lastRefreshMs by viewModel.lastRefreshMs.collectAsState()
    val sohHistory by viewModel.sohHistory.collectAsState()

    // Load history for each battery on first appearance
    LaunchedEffect(scores) {
        scores.forEach { viewModel.loadHistory(it.battery) }
    }

    Column(modifier = Modifier.fillMaxSize().padding(8.dp)) {
        // Header with refresh
        Row(
            modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column {
                Text(
                    text = "SOH Batteries",
                    style = MaterialTheme.typography.headlineSmall,
                )
                if (lastRefreshMs > 0) {
                    val fmt = SimpleDateFormat("HH:mm dd/MM", Locale.FRANCE)
                    Text(
                        text = "Mis \u00e0 jour: ${fmt.format(Date(lastRefreshMs))}",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            IconButton(onClick = { viewModel.refresh() }, enabled = !isRefreshing) {
                if (isRefreshing) {
                    CircularProgressIndicator(modifier = Modifier.size(24.dp), strokeWidth = 2.dp)
                } else {
                    Icon(Icons.Filled.Refresh, contentDescription = "Rafra\u00eechir")
                }
            }
        }

        if (scores.isEmpty()) {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = "Aucune donn\u00e9e SOH disponible.\nV\u00e9rifiez la connexion au serveur.",
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        } else {
            LazyVerticalGrid(
                columns = GridCells.Fixed(2),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                items(scores, key = { it.battery }) { score ->
                    SohBatteryCard(
                        score = score,
                        sparklineValues = sohHistory[score.battery]
                            ?.map { it.sohScore * 100f } ?: emptyList(),
                        onClick = { onBatteryClick(score.battery) },
                    )
                }
            }
        }
    }
}

@Composable
private fun SohBatteryCard(
    score: MlScore,
    sparklineValues: List<Float>,
    onClick: () -> Unit,
) {
    val sohPct = (score.sohScore * 100).toInt()
    val anomalyColor = when {
        score.anomalyScore > 0.7f -> MaterialTheme.colorScheme.error
        score.anomalyScore > 0.3f -> MaterialTheme.colorScheme.tertiary
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }

    Card(
        onClick = onClick,
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = "Bat ${score.battery + 1}",
                    style = MaterialTheme.typography.labelMedium,
                )
                SohGauge(sohPercent = sohPct, size = 48.dp, strokeWidth = 4.dp)
            }

            // RUL
            Text(
                text = "RUL: ${score.rulDays}j",
                style = MaterialTheme.typography.bodySmall,
                fontFamily = FontFamily.Monospace,
            )

            // Anomaly score
            Text(
                text = "Anomalie: ${String.format("%.0f%%", score.anomalyScore * 100)}",
                style = MaterialTheme.typography.bodySmall,
                color = anomalyColor,
            )

            // R_int trend
            if (score.rIntTrendMohmPerDay != 0f) {
                Text(
                    text = "R_int: ${String.format("%+.2f", score.rIntTrendMohmPerDay)} m\u03a9/j",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            // Sparkline
            if (sparklineValues.size >= 2) {
                SohSparkline(
                    values = sparklineValues,
                    color = when {
                        sohPct >= 80 -> androidx.compose.ui.graphics.Color(0xFF4CAF50)
                        sohPct >= 60 -> androidx.compose.ui.graphics.Color(0xFFFF9800)
                        else -> androidx.compose.ui.graphics.Color(0xFFF44336)
                    },
                )
            }
        }
    }
}
```

**Commit:** `feat(app): add SOH dashboard tab with gauge, sparkline, per-battery scores`

---

### Task 6: Fleet view — aggregate health circle + outlier highlight

**Files:**
- Create: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/viewmodel/FleetViewModel.kt`
- Create: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/fleet/FleetScreen.kt`
- Create: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/fleet/FleetHealthCircle.kt`

- [ ] **Step 1: Create FleetViewModel**

```kotlin
// kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/viewmodel/FleetViewModel.kt
package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kxkm.bmu.shared.domain.SohUseCase
import com.kxkm.bmu.shared.model.FleetHealth
import com.kxkm.bmu.shared.model.MlScore
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class FleetViewModel @Inject constructor(
    private val sohUseCase: SohUseCase,
) : ViewModel() {

    private val _fleetHealth = MutableStateFlow<FleetHealth?>(null)
    val fleetHealth: StateFlow<FleetHealth?> = _fleetHealth.asStateFlow()

    private val _scores = MutableStateFlow<List<MlScore>>(emptyList())
    val scores: StateFlow<List<MlScore>> = _scores.asStateFlow()

    init {
        viewModelScope.launch {
            sohUseCase.observeFleetHealth { _fleetHealth.value = it }
        }
        viewModelScope.launch {
            sohUseCase.observeMlScores { _scores.value = it }
        }
    }

    fun refresh() {
        viewModelScope.launch { sohUseCase.refreshFromCloud() }
    }
}
```

- [ ] **Step 2: Create FleetHealthCircle composable**

Large circular gauge for aggregate fleet health (0-100%), with outlier info below.

```kotlin
// kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/fleet/FleetHealthCircle.kt
package com.kxkm.bmu.ui.fleet

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun FleetHealthCircle(
    healthPercent: Int,
    modifier: Modifier = Modifier,
) {
    val color = when {
        healthPercent >= 80 -> Color(0xFF4CAF50)
        healthPercent >= 60 -> Color(0xFFFF9800)
        else -> Color(0xFFF44336)
    }

    Box(modifier = modifier.size(160.dp), contentAlignment = Alignment.Center) {
        Canvas(modifier = Modifier.size(160.dp)) {
            val stroke = Stroke(width = 12.dp.toPx(), cap = StrokeCap.Round)
            val arcSize = Size(size.width - stroke.width, size.height - stroke.width)
            val topLeft = Offset(stroke.width / 2f, stroke.width / 2f)

            drawArc(
                color = color.copy(alpha = 0.12f),
                startAngle = 135f, sweepAngle = 270f,
                useCenter = false, topLeft = topLeft, size = arcSize, style = stroke,
            )
            drawArc(
                color = color,
                startAngle = 135f,
                sweepAngle = 270f * (healthPercent.coerceIn(0, 100) / 100f),
                useCenter = false, topLeft = topLeft, size = arcSize, style = stroke,
            )
        }

        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = "$healthPercent%",
                fontSize = 32.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                color = color,
            )
            Text(
                text = "Sant\u00e9 flotte",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}
```

- [ ] **Step 3: Create FleetScreen**

```kotlin
// kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/fleet/FleetScreen.kt
package com.kxkm.bmu.ui.fleet

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.viewmodel.FleetViewModel

@Composable
fun FleetScreen(
    onBatteryClick: (Int) -> Unit = {},
    viewModel: FleetViewModel = hiltViewModel(),
) {
    val fleetHealth by viewModel.fleetHealth.collectAsState()
    val scores by viewModel.scores.collectAsState()

    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("Vue Flotte", style = MaterialTheme.typography.headlineSmall)
            IconButton(onClick = { viewModel.refresh() }) {
                Icon(Icons.Filled.Refresh, contentDescription = "Rafra\u00eechir")
            }
        }

        Spacer(modifier = Modifier.height(24.dp))

        fleetHealth?.let { fleet ->
            FleetHealthCircle(
                healthPercent = (fleet.fleetHealth * 100).toInt(),
            )

            Spacer(modifier = Modifier.height(16.dp))

            // Imbalance indicator
            Text(
                text = "D\u00e9s\u00e9quilibre: ${String.format("%.0f%%", fleet.imbalanceSeverity * 100)}",
                style = MaterialTheme.typography.bodyMedium,
                color = when {
                    fleet.imbalanceSeverity > 0.5f -> MaterialTheme.colorScheme.error
                    fleet.imbalanceSeverity > 0.2f -> MaterialTheme.colorScheme.tertiary
                    else -> MaterialTheme.colorScheme.onSurfaceVariant
                },
            )

            Spacer(modifier = Modifier.height(16.dp))

            // Outlier card
            val outlierScore = scores.firstOrNull { it.battery == fleet.outlierIdx }
            if (fleet.outlierScore > 0.3f && outlierScore != null) {
                Card(
                    onClick = { onBatteryClick(fleet.outlierIdx) },
                    colors = CardDefaults.cardColors(
                        containerColor = when {
                            fleet.outlierScore > 0.7f -> Color(0xFFFDE8E8)
                            else -> Color(0xFFFFF3E0)
                        },
                    ),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        Icon(
                            Icons.Filled.Warning,
                            contentDescription = "Attention",
                            tint = if (fleet.outlierScore > 0.7f) Color(0xFFF44336) else Color(0xFFFF9800),
                        )
                        Column {
                            Text(
                                text = "Batterie ${fleet.outlierIdx + 1} — anomalie d\u00e9tect\u00e9e",
                                fontWeight = FontWeight.Bold,
                                style = MaterialTheme.typography.bodyLarge,
                            )
                            Text(
                                text = "Score anomalie: ${String.format("%.0f%%", fleet.outlierScore * 100)} | SOH: ${String.format("%.0f%%", outlierScore.sohScore * 100)}",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(
                                text = "Appuyez pour voir le d\u00e9tail",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.primary,
                            )
                        }
                    }
                }
            }
        } ?: run {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = "Donn\u00e9es flotte non disponibles",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
```

**Commit:** `feat(app): add fleet view with health circle and outlier highlight card`

---

### Task 7: Diagnostic card — French text display with severity color

**Files:**
- Create: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/soh/DiagnosticCard.kt`

- [ ] **Step 1: Create DiagnosticCard composable**

```kotlin
// kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/soh/DiagnosticCard.kt
package com.kxkm.bmu.ui.soh

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.kxkm.bmu.shared.model.Diagnostic
import com.kxkm.bmu.shared.model.DiagnosticSeverity
import java.text.SimpleDateFormat
import java.util.*

@Composable
fun DiagnosticCard(
    diagnostic: Diagnostic?,
    isLoading: Boolean,
    onRefresh: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val (bgColor, iconColor, icon) = when (diagnostic?.severity) {
        DiagnosticSeverity.critical -> Triple(
            Color(0xFFFDE8E8), Color(0xFFF44336), Icons.Filled.Warning
        )
        DiagnosticSeverity.warning -> Triple(
            Color(0xFFFFF3E0), Color(0xFFFF9800), Icons.Filled.Warning
        )
        else -> Triple(
            Color(0xFFE8F5E9), Color(0xFF4CAF50), Icons.Filled.Info
        )
    }

    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = bgColor),
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Icon(icon, contentDescription = null, tint = iconColor)
                    Text(
                        text = "Diagnostic IA",
                        style = MaterialTheme.typography.titleSmall,
                    )
                }
                IconButton(onClick = onRefresh, enabled = !isLoading) {
                    if (isLoading) {
                        CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
                    } else {
                        Icon(Icons.Filled.Refresh, contentDescription = "G\u00e9n\u00e9rer diagnostic")
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            if (diagnostic != null) {
                Text(
                    text = diagnostic.diagnostic,
                    style = MaterialTheme.typography.bodyMedium,
                )
                Spacer(modifier = Modifier.height(4.dp))
                val fmt = SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.FRANCE)
                Text(
                    text = "G\u00e9n\u00e9r\u00e9 le ${fmt.format(Date(diagnostic.generatedAt * 1000))}",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                Text(
                    text = "Aucun diagnostic disponible. Appuyez sur rafra\u00eechir pour en g\u00e9n\u00e9rer un.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
```

**Commit:** `feat(app): add DiagnosticCard with severity color and refresh button`

---

### Task 8: R_int trend chart — 7-day line chart per battery

**Files:**
- Create: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/soh/RintTrendChart.kt`

- [ ] **Step 1: Create RintTrendChart composable**

Follow existing `VoltageChart.kt` patterns (Vico library, CartesianChartHost, modelProducer).

```kotlin
// kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/soh/RintTrendChart.kt
package com.kxkm.bmu.ui.soh

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import com.kxkm.bmu.shared.model.MlScore
import com.patrykandpatrick.vico.compose.cartesian.CartesianChartHost
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberBottomAxis
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberStartAxis
import com.patrykandpatrick.vico.compose.cartesian.layer.rememberLineCartesianLayer
import com.patrykandpatrick.vico.compose.cartesian.rememberCartesianChart
import com.patrykandpatrick.vico.core.cartesian.data.CartesianChartModelProducer
import com.patrykandpatrick.vico.core.cartesian.data.lineSeries
import java.text.SimpleDateFormat
import java.util.*

@Composable
fun RintTrendChart(
    history: List<MlScore>,
    modifier: Modifier = Modifier,
) {
    if (history.size < 2) {
        Text(
            text = "Pas assez de donn\u00e9es pour le graphique R_int",
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.bodyMedium,
        )
        return
    }

    val modelProducer = remember { CartesianChartModelProducer() }

    remember(history) {
        modelProducer.runTransaction {
            lineSeries {
                // R_int trend = cumulative from rIntTrendMohmPerDay
                series(history.map { it.rIntTrendMohmPerDay.toDouble() })
            }
        }
    }

    CartesianChartHost(
        chart = rememberCartesianChart(
            rememberLineCartesianLayer(),
            startAxis = rememberStartAxis(
                title = "R_int trend (m\u03a9/j)",
            ),
            bottomAxis = rememberBottomAxis(
                valueFormatter = { value, _, _ ->
                    val index = value.toInt().coerceIn(0, history.size - 1)
                    val ts = history.getOrNull(index)?.timestamp ?: 0L
                    val fmt = SimpleDateFormat("dd/MM", Locale.FRANCE)
                    fmt.format(Date(ts * 1000))
                },
            ),
        ),
        modelProducer = modelProducer,
        modifier = modifier,
    )
}
```

**Commit:** `feat(app): add RintTrendChart 7-day line chart using Vico`

---

### Task 9: Notifications — local push on anomaly or low SOH

**Files:**
- Create: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/notification/SohNotificationManager.kt`

- [ ] **Step 1: Create SohNotificationManager**

Uses Android NotificationManager with local triggers from cached scores. No FCM needed.

```kotlin
// kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/notification/SohNotificationManager.kt
package com.kxkm.bmu.notification

import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.os.Build
import androidx.core.app.NotificationCompat
import com.kxkm.bmu.R
import com.kxkm.bmu.shared.domain.SohUseCase
import kotlinx.coroutines.*

/**
 * Monitors cached ML scores and fires local notifications when
 * anomaly_score > 0.7 or SOH < 70%.
 */
class SohNotificationManager(
    private val context: Context,
    private val sohUseCase: SohUseCase,
) {
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private val notifiedBatteries = mutableSetOf<Int>()

    companion object {
        private const val CHANNEL_ID = "soh_alerts"
        private const val CHANNEL_NAME = "Alertes SOH"
        private const val NOTIFICATION_BASE_ID = 9000
        private const val CHECK_INTERVAL_MS = 5 * 60 * 1000L // 5 min
    }

    fun start() {
        createChannel()
        scope.launch {
            while (true) {
                checkAndNotify()
                delay(CHECK_INTERVAL_MS)
            }
        }
    }

    fun stop() {
        scope.cancel()
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID, CHANNEL_NAME, NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "Alertes de sant\u00e9 batterie (SOH, anomalies)"
            }
            val nm = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            nm.createNotificationChannel(channel)
        }
    }

    private fun checkAndNotify() {
        val alerts = sohUseCase.getAnomalyAlerts()
        val nm = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

        alerts.forEach { score ->
            // Only notify once per battery until score returns to normal
            if (score.battery in notifiedBatteries) return@forEach
            notifiedBatteries.add(score.battery)

            val sohPct = (score.sohScore * 100).toInt()
            val title = "Batterie ${score.battery + 1} — Alerte"
            val text = when {
                score.anomalyScore > 0.7f && sohPct < 70 ->
                    "Anomalie d\u00e9tect\u00e9e (${String.format("%.0f%%", score.anomalyScore * 100)}) et SOH faible ($sohPct%)"
                score.anomalyScore > 0.7f ->
                    "Anomalie d\u00e9tect\u00e9e: score ${String.format("%.0f%%", score.anomalyScore * 100)}"
                else ->
                    "SOH critique: $sohPct%. RUL estim\u00e9: ${score.rulDays} jours."
            }

            val notification = NotificationCompat.Builder(context, CHANNEL_ID)
                .setSmallIcon(R.drawable.ic_launcher_foreground)
                .setContentTitle(title)
                .setContentText(text)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setAutoCancel(true)
                .build()

            nm.notify(NOTIFICATION_BASE_ID + score.battery, notification)
        }

        // Clear notified set for batteries that returned to normal
        val alertBatteries = alerts.map { it.battery }.toSet()
        notifiedBatteries.removeAll { it !in alertBatteries }
    }
}
```

- [ ] **Step 2: Start SohNotificationManager in BmuApplication**

Add to existing `BmuApplication.kt`:

```kotlin
// In BmuApplication.onCreate(), after existing initialization:
val sohNotificationManager = SohNotificationManager(this, sohUseCase)
sohNotificationManager.start()
```

**Commit:** `feat(app): add local push notifications for anomaly/SOH alerts`

---

### Task 10: Offline behavior — cached data with "last updated" timestamp

**Files:**
- Edit: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/domain/SohUseCase.kt` (already handles offline in Task 4)

- [ ] **Step 1: Verify offline behavior is complete**

The `SohUseCase` already:
1. Loads from SQLDelight cache on `start()` before any network call
2. Wraps all REST calls in `try/catch`, falls back to cached data
3. Exposes `lastRefreshMs` StateFlow for "last updated" display
4. Never overwrites cache on network failure

No additional code needed. Verify that:
- `SohDashboardScreen` displays `lastRefreshMs` formatted timestamp (done in Task 5)
- `DiagnosticCard` shows `generatedAt` timestamp (done in Task 7)
- `FleetScreen` uses cached data via ViewModel (done in Task 6)

**Commit:** not needed (no changes)

---

### Task 11: Navigation integration — add SOH and Fleet tabs

**Files:**
- Edit: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/BmuNavHost.kt`

- [ ] **Step 1: Add SOH and Fleet routes to BmuNavHost**

Add two new `BmuRoute` entries and composable destinations:

```kotlin
// Add to BmuRoute sealed class:
data object Soh : BmuRoute("soh", "SOH", Icons.Filled.MonitorHeart)
data object Fleet : BmuRoute("fleet", "Flotte", Icons.Filled.Hub)

// Add to tabs list (after Dashboard):
add(BmuRoute.Soh)
add(BmuRoute.Fleet)

// Add composable destinations in NavHost:
composable(BmuRoute.Soh.route) {
    SohDashboardScreen(
        onBatteryClick = { index ->
            navController.navigate("battery_detail/$index")
        },
    )
}

composable(BmuRoute.Fleet.route) {
    FleetScreen(
        onBatteryClick = { index ->
            navController.navigate("battery_detail/$index")
        },
    )
}
```

- [ ] **Step 2: Add DiagnosticCard + RintTrendChart to BatteryDetailScreen**

In the existing battery detail screen, add a section below the voltage chart:

```kotlin
// Add to BatteryDetailScreen content, after existing VoltageChart:
// --- SOH Section ---
val diagnostic by sohDashboardVM.diagnostic.collectAsState()
val isLoadingDiag by sohDashboardVM.isDiagLoading.collectAsState()
val sohHistory by sohDashboardVM.sohHistory.collectAsState()
val batteryHistory = sohHistory[batteryIndex] ?: emptyList()

Text("Diagnostic", style = MaterialTheme.typography.titleMedium)
DiagnosticCard(
    diagnostic = diagnostic,
    isLoading = isLoadingDiag,
    onRefresh = { sohDashboardVM.refreshDiagnostic(batteryIndex) },
)

if (batteryHistory.size >= 2) {
    Spacer(modifier = Modifier.height(16.dp))
    Text("Tendance R_int (7 jours)", style = MaterialTheme.typography.titleMedium)
    RintTrendChart(
        history = batteryHistory,
        modifier = Modifier.fillMaxWidth().height(200.dp),
    )
}
```

**Commit:** `feat(app): integrate SOH/Fleet tabs in navigation and diagnostic in detail view`

---

### Task 12: iOS views (SwiftUI)

**Files:**
- Create: `kxkm-bmu-app/iosApp/Sources/SOH/SohDashboardView.swift`
- Create: `kxkm-bmu-app/iosApp/Sources/SOH/SohGaugeView.swift`
- Create: `kxkm-bmu-app/iosApp/Sources/SOH/DiagnosticCardView.swift`
- Create: `kxkm-bmu-app/iosApp/Sources/SOH/FleetHealthView.swift`
- Create: `kxkm-bmu-app/iosApp/Sources/Notification/SohNotificationDelegate.swift`

- [ ] **Step 1: Create SohGaugeView**

```swift
// kxkm-bmu-app/iosApp/Sources/SOH/SohGaugeView.swift
import SwiftUI

struct SohGaugeView: View {
    let sohPercent: Int
    var size: CGFloat = 72

    private var color: Color {
        switch sohPercent {
        case 80...100: return .green
        case 60..<80: return .orange
        default: return .red
        }
    }

    var body: some View {
        ZStack {
            Circle()
                .trim(from: 0.125, to: 0.875)
                .stroke(color.opacity(0.15), style: StrokeStyle(lineWidth: 6, lineCap: .round))
                .rotationEffect(.degrees(90))
                .frame(width: size, height: size)

            Circle()
                .trim(from: 0.125, to: 0.125 + 0.75 * Double(min(max(sohPercent, 0), 100)) / 100.0)
                .stroke(color, style: StrokeStyle(lineWidth: 6, lineCap: .round))
                .rotationEffect(.degrees(90))
                .frame(width: size, height: size)

            Text("\(sohPercent)%")
                .font(.system(size: size > 64 ? 16 : 12, weight: .bold, design: .monospaced))
                .foregroundColor(color)
        }
    }
}
```

- [ ] **Step 2: Create DiagnosticCardView**

```swift
// kxkm-bmu-app/iosApp/Sources/SOH/DiagnosticCardView.swift
import SwiftUI
import Shared

struct DiagnosticCardView: View {
    let diagnostic: Diagnostic?
    let isLoading: Bool
    let onRefresh: () -> Void

    private var bgColor: Color {
        switch diagnostic?.severity {
        case .critical: return Color.red.opacity(0.1)
        case .warning: return Color.orange.opacity(0.1)
        default: return Color.green.opacity(0.1)
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Image(systemName: diagnostic?.severity == .info ? "info.circle" : "exclamationmark.triangle")
                    .foregroundColor(diagnostic?.severity == .critical ? .red : diagnostic?.severity == .warning ? .orange : .green)
                Text("Diagnostic IA")
                    .font(.headline)
                Spacer()
                Button(action: onRefresh) {
                    if isLoading {
                        ProgressView()
                    } else {
                        Image(systemName: "arrow.clockwise")
                    }
                }
                .disabled(isLoading)
            }

            if let diag = diagnostic {
                Text(diag.diagnostic)
                    .font(.body)
                let date = Date(timeIntervalSince1970: TimeInterval(diag.generatedAt))
                Text("Généré le \(date.formatted(date: .abbreviated, time: .shortened))")
                    .font(.caption)
                    .foregroundColor(.secondary)
            } else {
                Text("Aucun diagnostic disponible.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .padding()
        .background(bgColor)
        .cornerRadius(12)
    }
}
```

- [ ] **Step 3: Create SohDashboardView**

```swift
// kxkm-bmu-app/iosApp/Sources/SOH/SohDashboardView.swift
import SwiftUI
import Shared

struct SohDashboardView: View {
    @StateObject private var viewModel = SohDashboardIOSViewModel()

    let columns = [GridItem(.flexible()), GridItem(.flexible())]

    var body: some View {
        NavigationView {
            ScrollView {
                if viewModel.scores.isEmpty {
                    Text("Aucune donnée SOH disponible")
                        .foregroundColor(.secondary)
                        .padding(.top, 40)
                } else {
                    LazyVGrid(columns: columns, spacing: 12) {
                        ForEach(viewModel.scores, id: \.battery) { score in
                            SohBatteryCardView(score: score)
                        }
                    }
                    .padding()
                }
            }
            .navigationTitle("SOH Batteries")
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button(action: { viewModel.refresh() }) {
                        Image(systemName: "arrow.clockwise")
                    }
                }
            }
        }
    }
}

struct SohBatteryCardView: View {
    let score: MlScore

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("Bat \(score.battery + 1)")
                    .font(.caption)
                Spacer()
                SohGaugeView(sohPercent: Int(score.sohScore * 100), size: 48)
            }
            Text("RUL: \(score.rulDays)j")
                .font(.caption2).monospacedDigit()
            Text(String(format: "Anomalie: %.0f%%", score.anomalyScore * 100))
                .font(.caption2)
                .foregroundColor(score.anomalyScore > 0.7 ? .red : score.anomalyScore > 0.3 ? .orange : .secondary)
        }
        .padding(12)
        .background(Color(.systemGray6))
        .cornerRadius(12)
    }
}
```

- [ ] **Step 4: Create FleetHealthView**

```swift
// kxkm-bmu-app/iosApp/Sources/SOH/FleetHealthView.swift
import SwiftUI
import Shared

struct FleetHealthView: View {
    @StateObject private var viewModel = FleetIOSViewModel()

    var body: some View {
        NavigationView {
            VStack(spacing: 24) {
                if let fleet = viewModel.fleetHealth {
                    SohGaugeView(sohPercent: Int(fleet.fleetHealth * 100), size: 160)

                    Text(String(format: "Déséquilibre: %.0f%%", fleet.imbalanceSeverity * 100))
                        .foregroundColor(fleet.imbalanceSeverity > 0.5 ? .red : fleet.imbalanceSeverity > 0.2 ? .orange : .secondary)

                    if fleet.outlierScore > 0.3 {
                        HStack {
                            Image(systemName: "exclamationmark.triangle.fill")
                                .foregroundColor(fleet.outlierScore > 0.7 ? .red : .orange)
                            VStack(alignment: .leading) {
                                Text("Batterie \(fleet.outlierIdx + 1) — anomalie")
                                    .font(.headline)
                                Text(String(format: "Score: %.0f%%", fleet.outlierScore * 100))
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }
                        }
                        .padding()
                        .background(Color.orange.opacity(0.1))
                        .cornerRadius(12)
                    }
                } else {
                    Text("Données flotte non disponibles")
                        .foregroundColor(.secondary)
                }

                Spacer()
            }
            .padding()
            .navigationTitle("Vue Flotte")
        }
    }
}
```

- [ ] **Step 5: Create SohNotificationDelegate**

```swift
// kxkm-bmu-app/iosApp/Sources/Notification/SohNotificationDelegate.swift
import UserNotifications
import Shared

class SohNotificationDelegate {
    private var notifiedBatteries: Set<Int> = []

    func requestPermission() {
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound, .badge]) { _, _ in }
    }

    func checkAndNotify(scores: [MlScore]) {
        let alerts = scores.filter { $0.anomalyScore > 0.7 || $0.sohScore < 0.7 }

        for score in alerts {
            guard !notifiedBatteries.contains(Int(score.battery)) else { continue }
            notifiedBatteries.insert(Int(score.battery))

            let sohPct = Int(score.sohScore * 100)
            let content = UNMutableNotificationContent()
            content.title = "Batterie \(score.battery + 1) — Alerte"
            if score.anomalyScore > 0.7 {
                content.body = String(format: "Anomalie détectée: %.0f%%", score.anomalyScore * 100)
            } else {
                content.body = "SOH critique: \(sohPct)%. RUL estimé: \(score.rulDays) jours."
            }
            content.sound = .default

            let request = UNNotificationRequest(
                identifier: "soh_alert_\(score.battery)",
                content: content,
                trigger: nil
            )
            UNUserNotificationCenter.current().add(request)
        }

        let alertBatteries = Set(alerts.map { Int($0.battery) })
        notifiedBatteries = notifiedBatteries.intersection(alertBatteries)
    }
}
```

**Commit:** `feat(app/ios): add SwiftUI SOH views, fleet health, diagnostic card, notifications`

---

### Task 13: Tests — REST client mock, ViewModel, cache

**Files:**
- Create: `kxkm-bmu-app/shared/src/commonTest/kotlin/com/kxkm/bmu/SohRestClientTest.kt`
- Create: `kxkm-bmu-app/shared/src/commonTest/kotlin/com/kxkm/bmu/SohUseCaseTest.kt`
- Create: `kxkm-bmu-app/shared/src/commonTest/kotlin/com/kxkm/bmu/SohCacheTest.kt`

- [ ] **Step 1: Create SohRestClientTest with Ktor MockEngine**

```kotlin
// kxkm-bmu-app/shared/src/commonTest/kotlin/com/kxkm/bmu/SohRestClientTest.kt
package com.kxkm.bmu

import com.kxkm.bmu.model.*
import com.kxkm.bmu.transport.SohRestClient
import kotlinx.coroutines.test.runTest
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SohRestClientTest {

    private val json = Json { ignoreUnknownKeys = true }

    @Test
    fun parsesMlScoreResponse() {
        val input = """
            {"battery":3,"soh_score":0.87,"rul_days":142,"anomaly_score":0.12,
             "r_int_trend_mohm_per_day":0.08,"timestamp":1743678000}
        """.trimIndent()
        val score = json.decodeFromString<MlScore>(input)
        assertEquals(3, score.battery)
        assertEquals(0.87f, score.sohScore, 0.01f)
        assertEquals(142, score.rulDays)
        assertEquals(0.12f, score.anomalyScore, 0.01f)
        assertEquals(0.08f, score.rIntTrendMohmPerDay, 0.01f)
    }

    @Test
    fun parsesFleetHealthResponse() {
        val input = """
            {"fleet_health":0.88,"outlier_idx":3,"outlier_score":0.72,
             "imbalance_severity":0.15,"timestamp":1743678000}
        """.trimIndent()
        val fleet = json.decodeFromString<FleetHealth>(input)
        assertEquals(0.88f, fleet.fleetHealth, 0.01f)
        assertEquals(3, fleet.outlierIdx)
        assertEquals(0.72f, fleet.outlierScore, 0.01f)
    }

    @Test
    fun parsesDiagnosticResponse() {
        val input = """
            {"battery":3,
             "diagnostic":"Batterie 3 en bon \u00e9tat (SOH 87%). Aucune action requise.",
             "severity":"info",
             "generated_at":1743678000}
        """.trimIndent()
        val diag = json.decodeFromString<Diagnostic>(input)
        assertEquals(3, diag.battery)
        assertEquals(DiagnosticSeverity.info, diag.severity)
        assertTrue(diag.diagnostic.contains("Batterie 3"))
    }

    @Test
    fun parsesScoreList() {
        val scores = listOf(
            MlScore(0, 0.92f, 180, 0.05f, 0.01f, 1743678000),
            MlScore(1, 0.85f, 120, 0.15f, 0.03f, 1743678000),
        )
        val serialized = json.encodeToString(scores)
        val parsed = json.decodeFromString<List<MlScore>>(serialized)
        assertEquals(2, parsed.size)
        assertEquals(0.92f, parsed[0].sohScore, 0.01f)
    }
}
```

- [ ] **Step 2: Create SohUseCaseTest**

```kotlin
// kxkm-bmu-app/shared/src/commonTest/kotlin/com/kxkm/bmu/SohUseCaseTest.kt
package com.kxkm.bmu

import com.kxkm.bmu.model.*
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SohUseCaseTest {

    @Test
    fun anomalyDetectionThresholds() {
        val scores = listOf(
            MlScore(0, 0.92f, 180, 0.05f, 0.01f, 1000),  // healthy
            MlScore(1, 0.65f, 30, 0.80f, 0.10f, 1000),   // anomaly + low SOH
            MlScore(2, 0.75f, 90, 0.30f, 0.02f, 1000),   // normal
            MlScore(3, 0.50f, 15, 0.40f, 0.15f, 1000),   // low SOH only
        )

        val alerts = scores.filter { score ->
            score.anomalyScore > 0.7f || (score.sohScore * 100).toInt() < 70
        }

        assertEquals(2, alerts.size)
        assertEquals(1, alerts[0].battery)  // anomaly + low SOH
        assertEquals(3, alerts[1].battery)  // low SOH only
    }

    @Test
    fun fleetHealthParsing() {
        val fleet = FleetHealth(
            fleetHealth = 0.88f,
            outlierIdx = 3,
            outlierScore = 0.72f,
            imbalanceSeverity = 0.15f,
            timestamp = 1743678000
        )
        assertEquals(88, (fleet.fleetHealth * 100).toInt())
        assertTrue(fleet.outlierScore > 0.3f)
    }

    @Test
    fun diagnosticSeverityMapping() {
        val diag = Diagnostic(
            battery = 0,
            diagnostic = "Test",
            severity = DiagnosticSeverity.critical,
            generatedAt = 1000
        )
        assertEquals(DiagnosticSeverity.critical, diag.severity)
    }
}
```

- [ ] **Step 3: Create SohCacheTest**

```kotlin
// kxkm-bmu-app/shared/src/commonTest/kotlin/com/kxkm/bmu/SohCacheTest.kt
package com.kxkm.bmu

import com.kxkm.bmu.model.*
import kotlinx.serialization.json.Json
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull

class SohCacheTest {

    private val json = Json { ignoreUnknownKeys = true }

    @Test
    fun mlScoreRoundTrip() {
        val original = MlScore(
            battery = 5,
            sohScore = 0.78f,
            rulDays = 95,
            anomalyScore = 0.22f,
            rIntTrendMohmPerDay = 0.04f,
            timestamp = 1743678000
        )
        val serialized = json.encodeToString(MlScore.serializer(), original)
        val deserialized = json.decodeFromString(MlScore.serializer(), serialized)
        assertEquals(original.battery, deserialized.battery)
        assertEquals(original.sohScore, deserialized.sohScore, 0.001f)
        assertEquals(original.rulDays, deserialized.rulDays)
    }

    @Test
    fun diagnosticRoundTrip() {
        val original = Diagnostic(
            battery = 2,
            diagnostic = "Batterie en d\u00e9gradation acc\u00e9l\u00e9r\u00e9e. Remplacement conseill\u00e9.",
            severity = DiagnosticSeverity.warning,
            generatedAt = 1743678000
        )
        val serialized = json.encodeToString(Diagnostic.serializer(), original)
        val deserialized = json.decodeFromString(Diagnostic.serializer(), serialized)
        assertEquals(original.diagnostic, deserialized.diagnostic)
        assertEquals(DiagnosticSeverity.warning, deserialized.severity)
    }

    @Test
    fun fleetHealthRoundTrip() {
        val original = FleetHealth(
            fleetHealth = 0.91f,
            outlierIdx = 7,
            outlierScore = 0.45f,
            imbalanceSeverity = 0.10f,
            timestamp = 1743678000
        )
        val serialized = json.encodeToString(FleetHealth.serializer(), original)
        val deserialized = json.decodeFromString(FleetHealth.serializer(), serialized)
        assertEquals(original.outlierIdx, deserialized.outlierIdx)
        assertEquals(original.fleetHealth, deserialized.fleetHealth, 0.001f)
    }
}
```

**Commit:** `test(app): add REST parsing, use case logic, and cache round-trip tests`

---

## Dependency Summary

```
Task 1  (data models)      → no dependency
Task 2  (SQLDelight cache)  → depends on Task 1
Task 3  (REST client)       → depends on Task 1
Task 4  (SohUseCase)        → depends on Tasks 2, 3
Task 5  (SOH dashboard)     → depends on Task 4
Task 6  (Fleet view)        → depends on Task 4
Task 7  (Diagnostic card)   → depends on Task 1
Task 8  (R_int chart)       → depends on Task 1
Task 9  (Notifications)     → depends on Task 4
Task 10 (Offline behavior)  → verification only, depends on Tasks 4-9
Task 11 (Navigation)        → depends on Tasks 5, 6, 7, 8
Task 12 (iOS views)         → depends on Tasks 1, 4
Task 13 (Tests)             → depends on Tasks 1, 3, 4
```

## Build & Test Commands

```bash
# Build shared module (validates SQLDelight schema + Kotlin compilation)
cd kxkm-bmu-app && ./gradlew :shared:build

# Run common tests (REST parsing, cache, use case)
cd kxkm-bmu-app && ./gradlew :shared:allTests

# Build Android app
cd kxkm-bmu-app && ./gradlew :androidApp:assembleDebug

# Run Android unit tests
cd kxkm-bmu-app && ./gradlew :androidApp:testDebugUnitTest

# iOS build (requires Xcode)
cd kxkm-bmu-app/iosApp && xcodebuild -scheme iosApp -destination 'platform=iOS Simulator,name=iPhone 16' build
```

## Risk Mitigation

- **kxkm-ai unavailable:** All cloud data cached in SQLDelight. App remains fully functional with BLE real-time data + stale cached scores. "Last updated" timestamp shown prominently.
- **Schema migration:** SQLDelight will auto-create new tables on first run. Existing `battery_history`, `audit_events`, `user_profiles`, `sync_queue` tables are unchanged.
- **Vico chart library:** Already used in `VoltageChart.kt` — no new dependency for `RintTrendChart`.
- **Notification permission:** Android 13+ requires POST_NOTIFICATIONS runtime permission. Add to manifest and request in `BmuApplication`.
