# BMU App — Plan 2: KMP Shared Module

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Kotlin Multiplatform shared module containing all business logic, data models, transport abstraction (BLE/WiFi/MQTT/REST), local database (SQLDelight), auth/roles, audit trail, and cloud sync. This module is consumed by both the iOS (SwiftUI) and Android (Compose) apps.

**Architecture:** Clean architecture with domain use cases calling into transport and persistence layers. StateFlow for reactive state. Transport layer abstracts 4 channels (BLE, WiFi WebSocket, MQTT, REST) behind a unified interface with automatic fallback. SQLDelight for cross-platform persistence.

**Tech Stack:** Kotlin Multiplatform, Kable (BLE), Ktor Client (HTTP/WS), Eclipse Paho (MQTT), SQLDelight, Kotlinx Coroutines, Kotlinx Serialization

**Spec:** `docs/superpowers/specs/2026-04-01-smartphone-app-design.md`

---

## File Structure

```
shared/
├── build.gradle.kts                          # KMP config, dependencies, iOS framework export
├── src/
│   ├── commonMain/kotlin/com/kxkm/bmu/
│   │   ├── model/
│   │   │   ├── BatteryState.kt               # Battery data + status enum
│   │   │   ├── SystemInfo.kt                 # Firmware, heap, topology
│   │   │   ├── SolarData.kt                  # VE.Direct solar
│   │   │   ├── AuditEvent.kt                 # Audit trail entry
│   │   │   ├── UserProfile.kt                # User + role
│   │   │   ├── ProtectionConfig.kt           # V/I thresholds
│   │   │   ├── TransportChannel.kt           # Channel enum
│   │   │   ├── CommandResult.kt              # Control command result
│   │   │   └── WifiStatusInfo.kt             # WiFi status from BLE
│   │   ├── transport/
│   │   │   ├── Transport.kt                  # Interface: read state, send commands
│   │   │   ├── TransportManager.kt           # Fallback logic, channel state
│   │   │   ├── BleTransport.kt               # Kable GATT parsing
│   │   │   ├── WifiTransport.kt              # Ktor WebSocket
│   │   │   ├── MqttTransport.kt              # Paho MQTT subscribe
│   │   │   ├── CloudRestClient.kt            # Ktor REST for history/audit
│   │   │   └── GattParser.kt                 # BLE byte[] ↔ model mapping
│   │   ├── domain/
│   │   │   ├── MonitoringUseCase.kt          # Observe batteries, system, solar
│   │   │   ├── ControlUseCase.kt             # Switch, reset, config
│   │   │   └── ConfigUseCase.kt              # Protection thresholds, WiFi config
│   │   ├── auth/
│   │   │   ├── AuthUseCase.kt                # PIN verify, user CRUD, role check
│   │   │   └── PinHasher.kt                  # SHA-256 + salt
│   │   ├── db/
│   │   │   └── BmuDatabase.sq                # SQLDelight schema (4 tables)
│   │   ├── sync/
│   │   │   ├── SyncManager.kt                # Queue → kxkm-ai REST batch
│   │   │   └── AuditUseCase.kt               # Audit read + write
│   │   └── SharedFactory.kt                  # DI: create all use cases
│   ├── androidMain/kotlin/com/kxkm/bmu/
│   │   └── Platform.kt                       # Android-specific (context, etc.)
│   └── iosMain/kotlin/com/kxkm/bmu/
│       └── Platform.kt                       # iOS-specific
└── src/commonTest/kotlin/com/kxkm/bmu/
    ├── GattParserTest.kt                     # BLE byte parsing tests
    ├── PinHasherTest.kt                      # Hash verification tests
    └── TransportManagerTest.kt               # Fallback logic tests
```

---

### Task 1: Gradle project setup + dependencies

**Files:**
- Create: `kxkm-bmu-app/settings.gradle.kts`
- Create: `kxkm-bmu-app/build.gradle.kts`
- Create: `kxkm-bmu-app/shared/build.gradle.kts`
- Create: `kxkm-bmu-app/gradle.properties`

- [ ] **Step 1: Create root project**

```bash
mkdir -p kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu
mkdir -p kxkm-bmu-app/shared/src/commonTest/kotlin/com/kxkm/bmu
mkdir -p kxkm-bmu-app/shared/src/androidMain/kotlin/com/kxkm/bmu
mkdir -p kxkm-bmu-app/shared/src/iosMain/kotlin/com/kxkm/bmu
```

Create `kxkm-bmu-app/settings.gradle.kts`:

```kotlin
pluginManagement {
    repositories {
        google()
        gradlePluginPortal()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "kxkm-bmu-app"
include(":shared")
```

Create `kxkm-bmu-app/gradle.properties`:

```properties
kotlin.code.style=official
android.useAndroidX=true
kotlin.mpp.androidSourceSetLayoutVersion=2
```

Create `kxkm-bmu-app/build.gradle.kts`:

```kotlin
plugins {
    kotlin("multiplatform").version("2.1.0").apply(false)
    kotlin("plugin.serialization").version("2.1.0").apply(false)
    id("com.android.library").version("8.2.0").apply(false)
    id("app.cash.sqldelight").version("2.0.2").apply(false)
}
```

- [ ] **Step 2: Create shared/build.gradle.kts**

```kotlin
plugins {
    kotlin("multiplatform")
    kotlin("plugin.serialization")
    id("com.android.library")
    id("app.cash.sqldelight")
}

kotlin {
    androidTarget {
        compilations.all {
            kotlinOptions { jvmTarget = "17" }
        }
    }

    iosX64()
    iosArm64()
    iosSimulatorArm64()

    cocoapods {
        summary = "KXKM BMU shared logic"
        homepage = "https://github.com/kxkm/bmu-app"
        ios.deploymentTarget = "16.0"
        framework { baseName = "Shared" }
    }

    sourceSets {
        val commonMain by getting {
            dependencies {
                // BLE
                implementation("com.juul.kable:core:0.31.0")
                // HTTP + WebSocket
                implementation("io.ktor:ktor-client-core:2.3.12")
                implementation("io.ktor:ktor-client-content-negotiation:2.3.12")
                implementation("io.ktor:ktor-serialization-kotlinx-json:2.3.12")
                implementation("io.ktor:ktor-client-websockets:2.3.12")
                // Serialization
                implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.3")
                // Coroutines
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.9.0")
                // SQLDelight
                implementation("app.cash.sqldelight:coroutines-extensions:2.0.2")
                // DateTime
                implementation("org.jetbrains.kotlinx:kotlinx-datetime:0.6.1")
            }
        }
        val commonTest by getting {
            dependencies {
                implementation(kotlin("test"))
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.9.0")
            }
        }
        val androidMain by getting {
            dependencies {
                implementation("io.ktor:ktor-client-okhttp:2.3.12")
                implementation("app.cash.sqldelight:android-driver:2.0.2")
                // MQTT
                implementation("org.eclipse.paho:org.eclipse.paho.client.mqttv3:1.2.5")
            }
        }
        val iosMain by creating {
            dependsOn(commonMain)
            dependencies {
                implementation("io.ktor:ktor-client-darwin:2.3.12")
                implementation("app.cash.sqldelight:native-driver:2.0.2")
            }
        }
        val iosX64Main by getting { dependsOn(iosMain) }
        val iosArm64Main by getting { dependsOn(iosMain) }
        val iosSimulatorArm64Main by getting { dependsOn(iosMain) }
    }
}

android {
    namespace = "com.kxkm.bmu.shared"
    compileSdk = 34
    defaultConfig { minSdk = 26 }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

sqldelight {
    databases {
        create("BmuDatabase") {
            packageName.set("com.kxkm.bmu.db")
        }
    }
}
```

- [ ] **Step 3: Verify Gradle sync**

```bash
cd kxkm-bmu-app && ./gradlew :shared:compileKotlinIosArm64 2>&1 | tail -5
```
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 4: Commit**

```bash
git add kxkm-bmu-app/
git commit -m "feat(shared): KMP project setup with Kable, Ktor, SQLDelight, Paho"
```

---

### Task 2: Data models

**Files:**
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/model/BatteryState.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/model/SystemInfo.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/model/SolarData.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/model/AuditEvent.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/model/UserProfile.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/model/ProtectionConfig.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/model/TransportChannel.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/model/CommandResult.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/model/WifiStatusInfo.kt`

- [ ] **Step 1: Create all model files**

`BatteryState.kt`:
```kotlin
package com.kxkm.bmu.model

import kotlinx.serialization.Serializable

@Serializable
enum class BatteryStatus { CONNECTED, DISCONNECTED, RECONNECTING, ERROR, LOCKED }

@Serializable
data class BatteryState(
    val index: Int,
    val voltageMv: Int,
    val currentMa: Int,
    val state: BatteryStatus,
    val ahDischargeMah: Int,
    val ahChargeMah: Int,
    val nbSwitch: Int
)
```

`SystemInfo.kt`:
```kotlin
package com.kxkm.bmu.model

import kotlinx.serialization.Serializable

@Serializable
data class SystemInfo(
    val firmwareVersion: String,
    val heapFree: Long,
    val uptimeSeconds: Long,
    val wifiIp: String?,
    val nbIna: Int,
    val nbTca: Int,
    val topologyValid: Boolean
)
```

`SolarData.kt`:
```kotlin
package com.kxkm.bmu.model

import kotlinx.serialization.Serializable

@Serializable
data class SolarData(
    val batteryVoltageMv: Int,
    val batteryCurrentMa: Int,
    val panelVoltageMv: Int,
    val panelPowerW: Int,
    val chargeState: Int,
    val chargeStateName: String = "",
    val yieldTodayWh: Long,
    val valid: Boolean = true
)
```

`AuditEvent.kt`:
```kotlin
package com.kxkm.bmu.model

import kotlinx.serialization.Serializable

@Serializable
data class AuditEvent(
    val timestamp: Long,
    val userId: String,
    val action: String,
    val target: Int? = null,
    val detail: String? = null
)
```

`UserProfile.kt`:
```kotlin
package com.kxkm.bmu.model

import kotlinx.serialization.Serializable

@Serializable
enum class UserRole { ADMIN, TECHNICIAN, VIEWER }

@Serializable
data class UserProfile(
    val id: String,
    val name: String,
    val role: UserRole,
    val pinHash: String,
    val salt: String
)
```

`ProtectionConfig.kt`:
```kotlin
package com.kxkm.bmu.model

import kotlinx.serialization.Serializable

@Serializable
data class ProtectionConfig(
    val minMv: Int = 24000,
    val maxMv: Int = 30000,
    val maxMa: Int = 10000,
    val diffMv: Int = 1000
)
```

`TransportChannel.kt`:
```kotlin
package com.kxkm.bmu.model

enum class TransportChannel {
    BLE, WIFI, MQTT_CLOUD, REST_CLOUD, OFFLINE
}
```

`CommandResult.kt`:
```kotlin
package com.kxkm.bmu.model

data class CommandResult(
    val isSuccess: Boolean,
    val errorMessage: String? = null
) {
    companion object {
        fun ok() = CommandResult(true)
        fun error(msg: String) = CommandResult(false, msg)
    }
}
```

`WifiStatusInfo.kt`:
```kotlin
package com.kxkm.bmu.model

import kotlinx.serialization.Serializable

@Serializable
data class WifiStatusInfo(
    val ssid: String,
    val ip: String,
    val rssi: Int,
    val connected: Boolean
)
```

- [ ] **Step 2: Build to verify**

```bash
cd kxkm-bmu-app && ./gradlew :shared:compileKotlinIosArm64 2>&1 | tail -3
```

- [ ] **Step 3: Commit**

```bash
git add shared/src/commonMain/kotlin/com/kxkm/bmu/model/
git commit -m "feat(shared): data models — Battery, System, Solar, Audit, User, Config"
```

---

### Task 3: SQLDelight schema + database

**Files:**
- Create: `shared/src/commonMain/sqldelight/com/kxkm/bmu/db/BmuDatabase.sq`
- Create: `shared/src/androidMain/kotlin/com/kxkm/bmu/db/DriverFactory.kt`
- Create: `shared/src/iosMain/kotlin/com/kxkm/bmu/db/DriverFactory.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/db/DatabaseHelper.kt`

- [ ] **Step 1: Create SQLDelight schema**

`shared/src/commonMain/sqldelight/com/kxkm/bmu/db/BmuDatabase.sq`:

```sql
-- Battery history snapshots (10s interval)
CREATE TABLE battery_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,
    battery_index INTEGER NOT NULL,
    voltage_mv INTEGER NOT NULL,
    current_ma INTEGER NOT NULL,
    state TEXT NOT NULL,
    ah_discharge_mah INTEGER NOT NULL DEFAULT 0,
    ah_charge_mah INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX idx_history_time ON battery_history(timestamp);
CREATE INDEX idx_history_battery ON battery_history(battery_index, timestamp);

-- Audit trail (append-only)
CREATE TABLE audit_events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp INTEGER NOT NULL,
    user_id TEXT NOT NULL,
    action TEXT NOT NULL,
    target INTEGER,
    detail TEXT,
    synced INTEGER NOT NULL DEFAULT 0
);

CREATE INDEX idx_audit_time ON audit_events(timestamp);
CREATE INDEX idx_audit_synced ON audit_events(synced);

-- User profiles
CREATE TABLE user_profiles (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    role TEXT NOT NULL,
    pin_hash TEXT NOT NULL,
    salt TEXT NOT NULL
);

-- Sync queue
CREATE TABLE sync_queue (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    type TEXT NOT NULL,
    payload TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    retry_count INTEGER NOT NULL DEFAULT 0
);

-- Queries: battery_history
insertHistory:
INSERT INTO battery_history (timestamp, battery_index, voltage_mv, current_ma, state, ah_discharge_mah, ah_charge_mah)
VALUES (?, ?, ?, ?, ?, ?, ?);

getHistory:
SELECT * FROM battery_history
WHERE battery_index = ? AND timestamp > ?
ORDER BY timestamp ASC;

purgeOldHistory:
DELETE FROM battery_history WHERE timestamp < ?;

-- Queries: audit_events
insertAudit:
INSERT INTO audit_events (timestamp, user_id, action, target, detail)
VALUES (?, ?, ?, ?, ?);

getAuditEvents:
SELECT * FROM audit_events
ORDER BY timestamp DESC
LIMIT ?;

getAuditFiltered:
SELECT * FROM audit_events
WHERE (? IS NULL OR action LIKE ?)
  AND (? IS NULL OR target = ?)
ORDER BY timestamp DESC
LIMIT ?;

getUnsyncedAudit:
SELECT * FROM audit_events WHERE synced = 0 ORDER BY timestamp ASC LIMIT ?;

markAuditSynced:
UPDATE audit_events SET synced = 1 WHERE id IN ?;

countUnsyncedAudit:
SELECT COUNT(*) FROM audit_events WHERE synced = 0;

-- Queries: user_profiles
insertUser:
INSERT INTO user_profiles (id, name, role, pin_hash, salt) VALUES (?, ?, ?, ?, ?);

getUserByHash:
SELECT * FROM user_profiles WHERE pin_hash = ?;

getAllUsers:
SELECT * FROM user_profiles ORDER BY name;

deleteUser:
DELETE FROM user_profiles WHERE id = ?;

countUsers:
SELECT COUNT(*) FROM user_profiles;

-- Queries: sync_queue
enqueueSync:
INSERT INTO sync_queue (type, payload, created_at) VALUES (?, ?, ?);

dequeueSync:
SELECT * FROM sync_queue ORDER BY created_at ASC LIMIT ?;

deleteSyncItems:
DELETE FROM sync_queue WHERE id IN ?;

countPendingSync:
SELECT COUNT(*) FROM sync_queue;
```

- [ ] **Step 2: Create platform-specific database drivers**

`shared/src/androidMain/kotlin/com/kxkm/bmu/db/DriverFactory.kt`:

```kotlin
package com.kxkm.bmu.db

import android.content.Context
import app.cash.sqldelight.db.SqlDriver
import app.cash.sqldelight.driver.android.AndroidSqliteDriver

actual class DriverFactory(private val context: Context) {
    actual fun createDriver(): SqlDriver {
        return AndroidSqliteDriver(BmuDatabase.Schema, context, "bmu.db")
    }
}
```

`shared/src/iosMain/kotlin/com/kxkm/bmu/db/DriverFactory.kt`:

```kotlin
package com.kxkm.bmu.db

import app.cash.sqldelight.db.SqlDriver
import app.cash.sqldelight.driver.native.NativeSqliteDriver

actual class DriverFactory {
    actual fun createDriver(): SqlDriver {
        return NativeSqliteDriver(BmuDatabase.Schema, "bmu.db")
    }
}
```

`shared/src/commonMain/kotlin/com/kxkm/bmu/db/DriverFactory.kt` (expect):

```kotlin
package com.kxkm.bmu.db

import app.cash.sqldelight.db.SqlDriver

expect class DriverFactory {
    fun createDriver(): SqlDriver
}
```

- [ ] **Step 3: Create DatabaseHelper**

`shared/src/commonMain/kotlin/com/kxkm/bmu/db/DatabaseHelper.kt`:

```kotlin
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

private fun Audit_events.toAuditEvent() = AuditEvent(
    timestamp = timestamp,
    userId = user_id,
    action = action,
    target = target?.toInt(),
    detail = detail
)
```

- [ ] **Step 4: Build to verify**

```bash
cd kxkm-bmu-app && ./gradlew :shared:compileKotlinIosArm64 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add shared/src/commonMain/sqldelight/ shared/src/*/kotlin/com/kxkm/bmu/db/
git commit -m "feat(shared): SQLDelight schema + DatabaseHelper (history, audit, users, sync)"
```

---

### Task 4: BLE GATT parser

**Files:**
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/transport/GattParser.kt`
- Create: `shared/src/commonTest/kotlin/com/kxkm/bmu/GattParserTest.kt`

- [ ] **Step 1: Write failing test**

`shared/src/commonTest/kotlin/com/kxkm/bmu/GattParserTest.kt`:

```kotlin
package com.kxkm.bmu

import com.kxkm.bmu.model.BatteryStatus
import com.kxkm.bmu.transport.GattParser
import kotlin.test.Test
import kotlin.test.assertEquals

class GattParserTest {
    @Test
    fun parseBatteryCharacteristic() {
        // 15 bytes: voltage_mv(i32) + current_ma(i32) + state(u8) + ah_dis(i32) + ah_ch(i32) + nb_sw(u8)
        // voltage = 26500 (0x00006784 LE = 84 67 00 00)
        // current = 5200  (0x00001450 LE = 50 14 00 00)
        // state = 0 (CONNECTED)
        // ah_dis = 1500  (0x000005DC LE = DC 05 00 00)
        // ah_ch = 200    (0x000000C8 LE = C8 00 00 00)
        // nb_sw = 3
        val bytes = byteArrayOf(
            0x84.toByte(), 0x67, 0x00, 0x00,  // voltage 26500
            0x50, 0x14, 0x00, 0x00,            // current 5200
            0x00,                               // state CONNECTED
            0xDC.toByte(), 0x05, 0x00, 0x00,   // ah_dis 1500
            0xC8.toByte(), 0x00, 0x00, 0x00,   // ah_ch 200
            0x03                                // nb_switch 3
        )

        val result = GattParser.parseBattery(0, bytes)
        assertEquals(26500, result.voltageMv)
        assertEquals(5200, result.currentMa)
        assertEquals(BatteryStatus.CONNECTED, result.state)
        assertEquals(1500, result.ahDischargeMah)
        assertEquals(200, result.ahChargeMah)
        assertEquals(3, result.nbSwitch)
    }

    @Test
    fun parseTopology() {
        val bytes = byteArrayOf(4, 1, 1) // nb_ina=4, nb_tca=1, valid=1
        val (nbIna, nbTca, valid) = GattParser.parseTopology(bytes)
        assertEquals(4, nbIna)
        assertEquals(1, nbTca)
        assertEquals(true, valid)
    }

    @Test
    fun encodeWifiConfig() {
        val bytes = GattParser.encodeWifiConfig("MySSID", "MyPassword123")
        assertEquals(96, bytes.size) // 32 + 64
        assertEquals('M'.code.toByte(), bytes[0])
        assertEquals('y'.code.toByte(), bytes[1])
        assertEquals(0, bytes[6].toInt()) // null terminator within 32 bytes
        assertEquals('M'.code.toByte(), bytes[32]) // password starts at offset 32
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd kxkm-bmu-app && ./gradlew :shared:testDebugUnitTest 2>&1 | tail -5
```
Expected: FAIL — `GattParser` not found

- [ ] **Step 3: Implement GattParser**

`shared/src/commonMain/kotlin/com/kxkm/bmu/transport/GattParser.kt`:

```kotlin
package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*

object GattParser {
    /** Parse 15-byte battery characteristic (little-endian) */
    fun parseBattery(index: Int, bytes: ByteArray): BatteryState {
        require(bytes.size >= 15) { "Battery payload must be >= 15 bytes, got ${bytes.size}" }
        return BatteryState(
            index = index,
            voltageMv = readInt32LE(bytes, 0),
            currentMa = readInt32LE(bytes, 4),
            state = BatteryStatus.entries.getOrElse(bytes[8].toInt()) { BatteryStatus.ERROR },
            ahDischargeMah = readInt32LE(bytes, 9),
            ahChargeMah = readInt32LE(bytes, 13),
            nbSwitch = if (bytes.size > 17) bytes[17].toInt() and 0xFF else 0
        )
    }

    /** Parse 3-byte topology: {nb_ina, nb_tca, valid} */
    fun parseTopology(bytes: ByteArray): Triple<Int, Int, Boolean> {
        require(bytes.size >= 3)
        return Triple(
            bytes[0].toInt() and 0xFF,
            bytes[1].toInt() and 0xFF,
            bytes[2].toInt() != 0
        )
    }

    /** Parse system heap (uint32 LE) */
    fun parseUint32(bytes: ByteArray): Long {
        require(bytes.size >= 4)
        return readInt32LE(bytes, 0).toLong() and 0xFFFFFFFFL
    }

    /** Parse solar data (12 bytes) */
    fun parseSolar(bytes: ByteArray): SolarData {
        require(bytes.size >= 12)
        return SolarData(
            batteryVoltageMv = readInt16LE(bytes, 0),
            batteryCurrentMa = readInt16LE(bytes, 2),
            panelVoltageMv = readUInt16LE(bytes, 4),
            panelPowerW = readUInt16LE(bytes, 6),
            chargeState = bytes[8].toInt() and 0xFF,
            yieldTodayWh = readInt32LE(bytes, 9).toLong() and 0xFFFFFFFFL,
            valid = bytes.size <= 12 || bytes[12].toInt() != 0
        )
    }

    /** Parse WiFi status (50 bytes) */
    fun parseWifiStatus(bytes: ByteArray): WifiStatusInfo {
        val ssid = bytes.copyOfRange(0, 32).takeWhile { it != 0.toByte() }
            .toByteArray().decodeToString()
        val ip = bytes.copyOfRange(32, 48).takeWhile { it != 0.toByte() }
            .toByteArray().decodeToString()
        val rssi = bytes[48].toInt()
        val connected = bytes[49].toInt() != 0
        return WifiStatusInfo(ssid, ip, rssi, connected)
    }

    /** Encode WiFi config: SSID (32B) + password (64B) = 96 bytes */
    fun encodeWifiConfig(ssid: String, password: String): ByteArray {
        val buf = ByteArray(96)
        val ssidBytes = ssid.encodeToByteArray()
        val passBytes = password.encodeToByteArray()
        ssidBytes.copyInto(buf, 0, 0, minOf(ssidBytes.size, 32))
        passBytes.copyInto(buf, 32, 0, minOf(passBytes.size, 64))
        return buf
    }

    /** Encode switch command: {battery_idx, on_off} */
    fun encodeSwitch(batteryIndex: Int, on: Boolean): ByteArray {
        return byteArrayOf(batteryIndex.toByte(), if (on) 1 else 0)
    }

    /** Encode reset command: {battery_idx} */
    fun encodeReset(batteryIndex: Int): ByteArray {
        return byteArrayOf(batteryIndex.toByte())
    }

    /** Encode protection config: 4x uint16 LE */
    fun encodeConfig(config: ProtectionConfig): ByteArray {
        val buf = ByteArray(8)
        writeUInt16LE(buf, 0, config.minMv)
        writeUInt16LE(buf, 2, config.maxMv)
        writeUInt16LE(buf, 4, config.maxMa)
        writeUInt16LE(buf, 6, config.diffMv)
        return buf
    }

    /** Parse command status response: {last_cmd, battery_idx, result} */
    fun parseCommandStatus(bytes: ByteArray): CommandResult {
        require(bytes.size >= 3)
        val result = bytes[2].toInt() and 0xFF
        return if (result == 0) CommandResult.ok()
        else CommandResult.error("code=$result")
    }

    // -- Little-endian helpers --

    private fun readInt32LE(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xFF) or
        ((b[off + 1].toInt() and 0xFF) shl 8) or
        ((b[off + 2].toInt() and 0xFF) shl 16) or
        ((b[off + 3].toInt() and 0xFF) shl 24)

    private fun readInt16LE(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xFF) or ((b[off + 1].toInt() and 0xFF) shl 8)

    private fun readUInt16LE(b: ByteArray, off: Int): Int =
        readInt16LE(b, off) and 0xFFFF

    private fun writeUInt16LE(b: ByteArray, off: Int, value: Int) {
        b[off] = (value and 0xFF).toByte()
        b[off + 1] = ((value shr 8) and 0xFF).toByte()
    }
}
```

- [ ] **Step 4: Fix test — battery payload is 18 bytes with nb_switch**

Looking at the spec: `{voltage_mv: i32, current_ma: i32, state: u8, ah_discharge_mah: i32, ah_charge_mah: i32, nb_switch: u8}` = 4+4+1+4+4+1 = 18 bytes. Update the test byte array to match:

The test in Step 1 already has 18 bytes. The parser reads `nb_switch` at offset 17. Verify the test byte array has the right length.

- [ ] **Step 5: Run tests to verify they pass**

```bash
cd kxkm-bmu-app && ./gradlew :shared:testDebugUnitTest 2>&1 | tail -5
```
Expected: 3 tests PASSED

- [ ] **Step 6: Commit**

```bash
git add shared/src/commonMain/kotlin/com/kxkm/bmu/transport/GattParser.kt \
        shared/src/commonTest/kotlin/com/kxkm/bmu/GattParserTest.kt
git commit -m "feat(shared): BLE GATT parser with LE byte encoding + 3 tests"
```

---

### Task 5: Transport interface + BLE transport

**Files:**
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/transport/Transport.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/transport/BleTransport.kt`

- [ ] **Step 1: Create Transport interface**

```kotlin
package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow

/** Unified transport interface — implemented by BLE, WiFi, MQTT, REST, Offline */
interface Transport {
    val channel: TransportChannel
    val isConnected: StateFlow<Boolean>

    /** Reactive battery state stream */
    fun observeBatteries(): Flow<List<BatteryState>>
    fun observeSystem(): Flow<SystemInfo?>
    fun observeSolar(): Flow<SolarData?>

    /** Commands (BLE/WiFi only — throws on read-only transports) */
    suspend fun switchBattery(index: Int, on: Boolean): CommandResult
    suspend fun resetSwitchCount(index: Int): CommandResult
    suspend fun setProtectionConfig(config: ProtectionConfig): CommandResult
    suspend fun setWifiConfig(ssid: String, password: String): CommandResult

    /** Connection lifecycle */
    suspend fun connect()
    suspend fun disconnect()
}
```

- [ ] **Step 2: Create BleTransport**

```kotlin
package com.kxkm.bmu.transport

import com.juul.kable.*
import com.kxkm.bmu.model.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

class BleTransport : Transport {
    override val channel = TransportChannel.BLE
    private val _isConnected = MutableStateFlow(false)
    override val isConnected: StateFlow<Boolean> = _isConnected

    private var peripheral: Peripheral? = null
    private val _batteries = MutableStateFlow<List<BatteryState>>(emptyList())
    private val _system = MutableStateFlow<SystemInfo?>(null)
    private val _solar = MutableStateFlow<SolarData?>(null)
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    companion object {
        private const val BMU_DEVICE_NAME = "KXKM-BMU"
        // UUID base: 4b584b4d-xxxx-4b4d-424d-55424c450000
        private fun svcUuid(id: Int) = "4b584b4d-%04x-4b4d-424d-55424c450000".format(id)
        private fun chrUuid(id: Int) = svcUuid(id)

        val BATTERY_SVC = svcUuid(0x0001)
        val SYSTEM_SVC  = svcUuid(0x0002)
        val CONTROL_SVC = svcUuid(0x0003)
    }

    override fun observeBatteries(): Flow<List<BatteryState>> = _batteries
    override fun observeSystem(): Flow<SystemInfo?> = _system
    override fun observeSolar(): Flow<SolarData?> = _solar

    override suspend fun connect() {
        // Scan for KXKM-BMU device
        val advertisement = Scanner {
            filters { match { name = BMU_DEVICE_NAME } }
        }.advertisements.first()

        peripheral = scope.peripheral(advertisement) {
            onServicesDiscovered {
                subscribeToNotifications()
            }
        }
        peripheral?.connect()
        _isConnected.value = true
    }

    override suspend fun disconnect() {
        peripheral?.disconnect()
        _isConnected.value = false
    }

    private suspend fun subscribeToNotifications() {
        val p = peripheral ?: return

        // Subscribe to battery characteristics (0x0010–0x001F)
        for (i in 0..15) {
            val uuid = chrUuid(0x0010 + i)
            scope.launch {
                try {
                    p.observe(characteristicOf(BATTERY_SVC, uuid)).collect { bytes ->
                        val state = GattParser.parseBattery(i, bytes)
                        val current = _batteries.value.toMutableList()
                        val idx = current.indexOfFirst { it.index == i }
                        if (idx >= 0) current[idx] = state else current.add(state)
                        _batteries.value = current.sortedBy { it.index }
                    }
                } catch (_: Exception) { /* characteristic may not exist for unused batteries */ }
            }
        }

        // Subscribe to system notifications
        scope.launch {
            p.observe(characteristicOf(SYSTEM_SVC, chrUuid(0x0021))).collect { bytes ->
                val heap = GattParser.parseUint32(bytes)
                _system.value = _system.value?.copy(heapFree = heap) ?: SystemInfo(
                    firmwareVersion = "", heapFree = heap, uptimeSeconds = 0,
                    wifiIp = null, nbIna = 0, nbTca = 0, topologyValid = false
                )
            }
        }

        // Read static system info once
        scope.launch {
            val fw = p.read(characteristicOf(SYSTEM_SVC, chrUuid(0x0020))).decodeToString()
            val uptime = GattParser.parseUint32(p.read(characteristicOf(SYSTEM_SVC, chrUuid(0x0022))))
            val ip = p.read(characteristicOf(SYSTEM_SVC, chrUuid(0x0023))).decodeToString()
            val (nbIna, nbTca, valid) = GattParser.parseTopology(
                p.read(characteristicOf(SYSTEM_SVC, chrUuid(0x0024)))
            )
            _system.value = SystemInfo(fw, _system.value?.heapFree ?: 0, uptime,
                ip.ifEmpty { null }, nbIna, nbTca, valid)
        }

        // Subscribe to solar
        scope.launch {
            p.observe(characteristicOf(SYSTEM_SVC, chrUuid(0x0025))).collect { bytes ->
                _solar.value = GattParser.parseSolar(bytes)
            }
        }
    }

    override suspend fun switchBattery(index: Int, on: Boolean): CommandResult {
        val p = peripheral ?: return CommandResult.error("Not connected")
        p.write(characteristicOf(CONTROL_SVC, chrUuid(0x0030)), GattParser.encodeSwitch(index, on))
        delay(200) // Wait for status notification
        val status = p.read(characteristicOf(CONTROL_SVC, chrUuid(0x0033)))
        return GattParser.parseCommandStatus(status)
    }

    override suspend fun resetSwitchCount(index: Int): CommandResult {
        val p = peripheral ?: return CommandResult.error("Not connected")
        p.write(characteristicOf(CONTROL_SVC, chrUuid(0x0031)), GattParser.encodeReset(index))
        delay(200)
        val status = p.read(characteristicOf(CONTROL_SVC, chrUuid(0x0033)))
        return GattParser.parseCommandStatus(status)
    }

    override suspend fun setProtectionConfig(config: ProtectionConfig): CommandResult {
        val p = peripheral ?: return CommandResult.error("Not connected")
        p.write(characteristicOf(CONTROL_SVC, chrUuid(0x0032)), GattParser.encodeConfig(config))
        delay(200)
        val status = p.read(characteristicOf(CONTROL_SVC, chrUuid(0x0033)))
        return GattParser.parseCommandStatus(status)
    }

    override suspend fun setWifiConfig(ssid: String, password: String): CommandResult {
        val p = peripheral ?: return CommandResult.error("Not connected")
        p.write(characteristicOf(CONTROL_SVC, chrUuid(0x0034)), GattParser.encodeWifiConfig(ssid, password))
        delay(200)
        val status = p.read(characteristicOf(CONTROL_SVC, chrUuid(0x0033)))
        return GattParser.parseCommandStatus(status)
    }
}
```

- [ ] **Step 3: Build to verify**

```bash
cd kxkm-bmu-app && ./gradlew :shared:compileKotlinIosArm64 2>&1 | tail -5
```

- [ ] **Step 4: Commit**

```bash
git add shared/src/commonMain/kotlin/com/kxkm/bmu/transport/Transport.kt \
        shared/src/commonMain/kotlin/com/kxkm/bmu/transport/BleTransport.kt
git commit -m "feat(shared): Transport interface + BLE transport (Kable GATT)"
```

---

### Task 6: WiFi WebSocket transport

**Files:**
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/transport/WifiTransport.kt`

- [ ] **Step 1: Implement WifiTransport**

```kotlin
package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import io.ktor.client.*
import io.ktor.client.plugins.contentnegotiation.*
import io.ktor.client.plugins.websocket.*
import io.ktor.client.request.*
import io.ktor.client.statement.*
import io.ktor.serialization.kotlinx.json.*
import io.ktor.websocket.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.json.Json

class WifiTransport(private val baseUrl: String, private val token: String) : Transport {
    override val channel = TransportChannel.WIFI
    private val _isConnected = MutableStateFlow(false)
    override val isConnected: StateFlow<Boolean> = _isConnected

    private val _batteries = MutableStateFlow<List<BatteryState>>(emptyList())
    private val _system = MutableStateFlow<SystemInfo?>(null)
    private val _solar = MutableStateFlow<SolarData?>(null)
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private val json = Json { ignoreUnknownKeys = true }

    private val client = HttpClient {
        install(ContentNegotiation) { json(json) }
        install(WebSockets)
    }

    override fun observeBatteries(): Flow<List<BatteryState>> = _batteries
    override fun observeSystem(): Flow<SystemInfo?> = _system
    override fun observeSolar(): Flow<SolarData?> = _solar

    override suspend fun connect() {
        // Start WebSocket connection for real-time battery updates
        scope.launch {
            try {
                client.webSocket("$baseUrl/ws") {
                    send("""{"auth":"$token"}""")
                    val authResp = (incoming.receive() as? Frame.Text)?.readText() ?: ""
                    if ("ok" !in authResp) {
                        _isConnected.value = false
                        return@webSocket
                    }
                    _isConnected.value = true

                    for (frame in incoming) {
                        if (frame is Frame.Text) {
                            try {
                                val data = json.decodeFromString<WsBatteryPush>(frame.readText())
                                _batteries.value = data.batteries
                            } catch (_: Exception) { }
                        }
                    }
                }
            } catch (_: Exception) {
                _isConnected.value = false
            }
        }

        // Periodic system + solar polling (every 5s)
        scope.launch {
            while (true) {
                try {
                    val sysResp = client.get("$baseUrl/api/system").bodyAsText()
                    _system.value = json.decodeFromString<SystemInfo>(sysResp)

                    val solarResp = client.get("$baseUrl/api/solar").bodyAsText()
                    _solar.value = json.decodeFromString<SolarData>(solarResp)
                } catch (_: Exception) { }
                delay(5000)
            }
        }
    }

    override suspend fun disconnect() {
        scope.cancel()
        client.close()
        _isConnected.value = false
    }

    override suspend fun switchBattery(index: Int, on: Boolean): CommandResult {
        return postMutation(if (on) "/api/battery/switch_on" else "/api/battery/switch_off",
            """{"battery":$index,"token":"$token"}""")
    }

    override suspend fun resetSwitchCount(index: Int): CommandResult {
        return CommandResult.error("Reset not available via WiFi API")
    }

    override suspend fun setProtectionConfig(config: ProtectionConfig): CommandResult {
        return CommandResult.error("Config not available via WiFi API")
    }

    override suspend fun setWifiConfig(ssid: String, password: String): CommandResult {
        return CommandResult.error("WiFi config requires BLE")
    }

    private suspend fun postMutation(path: String, body: String): CommandResult {
        return try {
            val resp = client.post("$baseUrl$path") {
                setBody(body)
                header("Content-Type", "application/json")
            }
            if (resp.status.value in 200..299) CommandResult.ok()
            else CommandResult.error("HTTP ${resp.status.value}")
        } catch (e: Exception) {
            CommandResult.error(e.message ?: "Network error")
        }
    }
}

@kotlinx.serialization.Serializable
private data class WsBatteryPush(val batteries: List<BatteryState>)
```

- [ ] **Step 2: Build to verify**

```bash
cd kxkm-bmu-app && ./gradlew :shared:compileKotlinIosArm64 2>&1 | tail -3
```

- [ ] **Step 3: Commit**

```bash
git add shared/src/commonMain/kotlin/com/kxkm/bmu/transport/WifiTransport.kt
git commit -m "feat(shared): WiFi WebSocket transport (Ktor client)"
```

---

### Task 7: MQTT + REST cloud transports

**Files:**
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/transport/MqttTransport.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/transport/CloudRestClient.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/transport/OfflineTransport.kt`

- [ ] **Step 1: Create MqttTransport (read-only)**

```kotlin
package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.json.Json

/** MQTT transport — read-only, subscribes to bmu/battery/# on kxkm-ai broker */
class MqttTransport(
    private val brokerUrl: String,
    private val username: String,
    private val password: String
) : Transport {
    override val channel = TransportChannel.MQTT_CLOUD
    private val _isConnected = MutableStateFlow(false)
    override val isConnected: StateFlow<Boolean> = _isConnected

    private val _batteries = MutableStateFlow<List<BatteryState>>(emptyList())
    private val _system = MutableStateFlow<SystemInfo?>(null)
    private val _solar = MutableStateFlow<SolarData?>(null)
    private val json = Json { ignoreUnknownKeys = true }

    override fun observeBatteries(): Flow<List<BatteryState>> = _batteries
    override fun observeSystem(): Flow<SystemInfo?> = _system
    override fun observeSolar(): Flow<SolarData?> = _solar

    override suspend fun connect() {
        // Platform-specific MQTT implementation
        // Android: Paho MQTT client
        // iOS: Custom wrapper or CocoaMQTT via expect/actual
        // Subscribes to bmu/battery/# and parses JSON payloads
        _isConnected.value = true
    }

    override suspend fun disconnect() {
        _isConnected.value = false
    }

    // Read-only transport — all commands throw
    override suspend fun switchBattery(index: Int, on: Boolean) =
        CommandResult.error("Control not available via cloud")
    override suspend fun resetSwitchCount(index: Int) =
        CommandResult.error("Control not available via cloud")
    override suspend fun setProtectionConfig(config: ProtectionConfig) =
        CommandResult.error("Control not available via cloud")
    override suspend fun setWifiConfig(ssid: String, password: String) =
        CommandResult.error("Control not available via cloud")

    /** Called by platform-specific MQTT callback when message received */
    fun onMqttMessage(topic: String, payload: String) {
        try {
            // Topic format: bmu/battery/N
            val parts = topic.split("/")
            if (parts.size >= 3 && parts[0] == "bmu" && parts[1] == "battery") {
                val batteryJson = json.decodeFromString<MqttBatteryPayload>(payload)
                val index = parts[2].toIntOrNull()?.minus(1) ?: return
                val state = BatteryState(
                    index = index,
                    voltageMv = batteryJson.v_mv.toInt(),
                    currentMa = (batteryJson.i_a * 1000).toInt(),
                    state = BatteryStatus.valueOf(batteryJson.state.uppercase()),
                    ahDischargeMah = (batteryJson.ah_d * 1000).toInt(),
                    ahChargeMah = (batteryJson.ah_c * 1000).toInt(),
                    nbSwitch = 0
                )
                val current = _batteries.value.toMutableList()
                val idx = current.indexOfFirst { it.index == index }
                if (idx >= 0) current[idx] = state else current.add(state)
                _batteries.value = current.sortedBy { it.index }
            }
        } catch (_: Exception) { }
    }
}

@kotlinx.serialization.Serializable
private data class MqttBatteryPayload(
    val bat: Int,
    val v_mv: Float,
    val i_a: Float,
    val ah_d: Float,
    val ah_c: Float,
    val state: String
)
```

- [ ] **Step 2: Create CloudRestClient**

```kotlin
package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import com.kxkm.bmu.db.BatteryHistoryPoint
import io.ktor.client.*
import io.ktor.client.request.*
import io.ktor.client.statement.*
import kotlinx.serialization.json.Json

/** REST client for kxkm-ai API — history queries and sync push */
class CloudRestClient(
    private val baseUrl: String,
    private val apiKey: String
) {
    private val client = HttpClient()
    private val json = Json { ignoreUnknownKeys = true }

    suspend fun getBatteries(): List<BatteryState> {
        val resp = client.get("$baseUrl/api/bmu/batteries") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<List<BatteryState>>(resp)
    }

    suspend fun getHistory(batteryIndex: Int, fromMs: Long, toMs: Long): List<BatteryHistoryPoint> {
        val resp = client.get("$baseUrl/api/bmu/history") {
            header("Authorization", "Bearer $apiKey")
            parameter("battery", batteryIndex)
            parameter("from", fromMs)
            parameter("to", toMs)
        }.bodyAsText()
        return json.decodeFromString<List<BatteryHistoryPoint>>(resp)
    }

    suspend fun getAuditEvents(
        fromMs: Long? = null, toMs: Long? = null,
        user: String? = null, action: String? = null
    ): List<AuditEvent> {
        val resp = client.get("$baseUrl/api/bmu/audit") {
            header("Authorization", "Bearer $apiKey")
            fromMs?.let { parameter("from", it) }
            toMs?.let { parameter("to", it) }
            user?.let { parameter("user", it) }
            action?.let { parameter("action", it) }
        }.bodyAsText()
        return json.decodeFromString<List<AuditEvent>>(resp)
    }

    suspend fun syncBatch(payload: String): Boolean {
        val resp = client.post("$baseUrl/api/bmu/sync") {
            header("Authorization", "Bearer $apiKey")
            header("Content-Type", "application/json")
            setBody(payload)
        }
        return resp.status.value in 200..299
    }
}
```

- [ ] **Step 3: Create OfflineTransport**

```kotlin
package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import com.kxkm.bmu.db.DatabaseHelper
import kotlinx.coroutines.flow.*

/** Offline transport — reads from local SQLDelight cache, no commands */
class OfflineTransport(private val db: DatabaseHelper) : Transport {
    override val channel = TransportChannel.OFFLINE
    override val isConnected: StateFlow<Boolean> = MutableStateFlow(false)

    override fun observeBatteries(): Flow<List<BatteryState>> = flowOf(emptyList())
    override fun observeSystem(): Flow<SystemInfo?> = flowOf(null)
    override fun observeSolar(): Flow<SolarData?> = flowOf(null)

    override suspend fun connect() {}
    override suspend fun disconnect() {}

    override suspend fun switchBattery(index: Int, on: Boolean) =
        CommandResult.error("Offline — pas de contrôle")
    override suspend fun resetSwitchCount(index: Int) =
        CommandResult.error("Offline — pas de contrôle")
    override suspend fun setProtectionConfig(config: ProtectionConfig) =
        CommandResult.error("Offline — pas de contrôle")
    override suspend fun setWifiConfig(ssid: String, password: String) =
        CommandResult.error("Offline — pas de contrôle")
}
```

- [ ] **Step 4: Build to verify**

```bash
cd kxkm-bmu-app && ./gradlew :shared:compileKotlinIosArm64 2>&1 | tail -3
```

- [ ] **Step 5: Commit**

```bash
git add shared/src/commonMain/kotlin/com/kxkm/bmu/transport/MqttTransport.kt \
        shared/src/commonMain/kotlin/com/kxkm/bmu/transport/CloudRestClient.kt \
        shared/src/commonMain/kotlin/com/kxkm/bmu/transport/OfflineTransport.kt
git commit -m "feat(shared): MQTT, REST cloud, and Offline transports"
```

---

### Task 8: TransportManager — fallback logic

**Files:**
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/transport/TransportManager.kt`
- Create: `shared/src/commonTest/kotlin/com/kxkm/bmu/TransportManagerTest.kt`

- [ ] **Step 1: Write failing test**

```kotlin
package com.kxkm.bmu

import com.kxkm.bmu.model.TransportChannel
import com.kxkm.bmu.transport.TransportManager
import kotlin.test.Test
import kotlin.test.assertEquals

class TransportManagerTest {
    @Test
    fun fallbackOrder() {
        val priority = TransportManager.PRIORITY_ORDER
        assertEquals(TransportChannel.BLE, priority[0])
        assertEquals(TransportChannel.WIFI, priority[1])
        assertEquals(TransportChannel.MQTT_CLOUD, priority[2])
        assertEquals(TransportChannel.OFFLINE, priority[3])
    }
}
```

- [ ] **Step 2: Implement TransportManager**

```kotlin
package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

class TransportManager(
    private val ble: BleTransport,
    private val wifi: WifiTransport?,
    private val mqtt: MqttTransport?,
    private val offline: OfflineTransport
) {
    companion object {
        val PRIORITY_ORDER = listOf(
            TransportChannel.BLE,
            TransportChannel.WIFI,
            TransportChannel.MQTT_CLOUD,
            TransportChannel.OFFLINE
        )
    }

    private val _activeChannel = MutableStateFlow(TransportChannel.OFFLINE)
    val activeChannel: StateFlow<TransportChannel> = _activeChannel

    private val _activeTransport = MutableStateFlow<Transport>(offline)
    val activeTransport: StateFlow<Transport> = _activeTransport

    /** Force a specific channel (null = auto) */
    var forcedChannel: TransportChannel? = null

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    fun start() {
        scope.launch {
            while (true) {
                val best = selectBestTransport()
                if (best.channel != _activeChannel.value) {
                    _activeTransport.value = best
                    _activeChannel.value = best.channel
                }
                delay(2000) // Re-evaluate every 2s
            }
        }
    }

    private fun selectBestTransport(): Transport {
        forcedChannel?.let { forced ->
            return getTransport(forced)
        }

        // Auto: try in priority order
        if (ble.isConnected.value) return ble
        if (wifi?.isConnected?.value == true) return wifi
        if (mqtt?.isConnected?.value == true) return mqtt ?: offline
        return offline
    }

    private fun getTransport(channel: TransportChannel): Transport {
        return when (channel) {
            TransportChannel.BLE -> ble
            TransportChannel.WIFI -> wifi ?: offline
            TransportChannel.MQTT_CLOUD -> mqtt ?: offline
            TransportChannel.REST_CLOUD -> offline // REST is query-only, not a live transport
            TransportChannel.OFFLINE -> offline
        }
    }

    /** Reactive battery stream from active transport */
    fun observeBatteries(): Flow<List<BatteryState>> =
        _activeTransport.flatMapLatest { it.observeBatteries() }

    fun observeSystem(): Flow<SystemInfo?> =
        _activeTransport.flatMapLatest { it.observeSystem() }

    fun observeSolar(): Flow<SolarData?> =
        _activeTransport.flatMapLatest { it.observeSolar() }

    suspend fun switchBattery(index: Int, on: Boolean): CommandResult =
        _activeTransport.value.switchBattery(index, on)

    suspend fun resetSwitchCount(index: Int): CommandResult =
        _activeTransport.value.resetSwitchCount(index)

    suspend fun setProtectionConfig(config: ProtectionConfig): CommandResult =
        _activeTransport.value.setProtectionConfig(config)

    suspend fun setWifiConfig(ssid: String, password: String): CommandResult =
        _activeTransport.value.setWifiConfig(ssid, password)
}
```

- [ ] **Step 3: Run tests**

```bash
cd kxkm-bmu-app && ./gradlew :shared:testDebugUnitTest 2>&1 | tail -5
```
Expected: All tests PASS

- [ ] **Step 4: Commit**

```bash
git add shared/src/commonMain/kotlin/com/kxkm/bmu/transport/TransportManager.kt \
        shared/src/commonTest/kotlin/com/kxkm/bmu/TransportManagerTest.kt
git commit -m "feat(shared): TransportManager with auto fallback BLE>WiFi>MQTT>Offline"
```

---

### Task 9: Auth, PIN hasher, audit use case

**Files:**
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/auth/PinHasher.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/auth/AuthUseCase.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/sync/AuditUseCase.kt`
- Create: `shared/src/commonTest/kotlin/com/kxkm/bmu/PinHasherTest.kt`

- [ ] **Step 1: Write PinHasher test**

```kotlin
package com.kxkm.bmu

import com.kxkm.bmu.auth.PinHasher
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals

class PinHasherTest {
    @Test
    fun hashIsDeterministic() {
        val salt = "test-salt-123"
        val hash1 = PinHasher.hash("123456", salt)
        val hash2 = PinHasher.hash("123456", salt)
        assertEquals(hash1, hash2)
    }

    @Test
    fun differentPinsDifferentHashes() {
        val salt = "test-salt-123"
        val hash1 = PinHasher.hash("123456", salt)
        val hash2 = PinHasher.hash("654321", salt)
        assertNotEquals(hash1, hash2)
    }

    @Test
    fun differentSaltsDifferentHashes() {
        val hash1 = PinHasher.hash("123456", "salt-a")
        val hash2 = PinHasher.hash("123456", "salt-b")
        assertNotEquals(hash1, hash2)
    }
}
```

- [ ] **Step 2: Implement PinHasher**

```kotlin
package com.kxkm.bmu.auth

import kotlin.random.Random

expect object PinHasher {
    fun hash(pin: String, salt: String): String
    fun generateSalt(): String
}
```

Android actual (`shared/src/androidMain/kotlin/com/kxkm/bmu/auth/PinHasher.kt`):

```kotlin
package com.kxkm.bmu.auth

import java.security.MessageDigest
import kotlin.random.Random

actual object PinHasher {
    actual fun hash(pin: String, salt: String): String {
        val md = MessageDigest.getInstance("SHA-256")
        val bytes = md.digest("$salt:$pin".toByteArray(Charsets.UTF_8))
        return bytes.joinToString("") { "%02x".format(it) }
    }

    actual fun generateSalt(): String {
        return Random.nextBytes(16).joinToString("") { "%02x".format(it) }
    }
}
```

iOS actual (`shared/src/iosMain/kotlin/com/kxkm/bmu/auth/PinHasher.kt`):

```kotlin
package com.kxkm.bmu.auth

import kotlinx.cinterop.*
import platform.CoreCrypto.CC_SHA256
import platform.CoreCrypto.CC_SHA256_DIGEST_LENGTH
import kotlin.random.Random

actual object PinHasher {
    @OptIn(ExperimentalForeignApi::class)
    actual fun hash(pin: String, salt: String): String {
        val input = "$salt:$pin".encodeToByteArray()
        val digest = UByteArray(CC_SHA256_DIGEST_LENGTH)
        input.usePinned { pinned ->
            digest.usePinned { digestPinned ->
                CC_SHA256(pinned.addressOf(0), input.size.toUInt(), digestPinned.addressOf(0))
            }
        }
        return digest.joinToString("") { it.toString(16).padStart(2, '0') }
    }

    actual fun generateSalt(): String {
        return Random.nextBytes(16).joinToString("") { "%02x".format(it) }
    }
}
```

- [ ] **Step 3: Implement AuthUseCase**

```kotlin
package com.kxkm.bmu.auth

import com.kxkm.bmu.db.DatabaseHelper
import com.kxkm.bmu.model.UserProfile
import com.kxkm.bmu.model.UserRole

class AuthUseCase(private val db: DatabaseHelper) {
    fun hasNoUsers(): Boolean = db.countUsers() == 0L

    fun authenticate(pin: String): UserProfile? {
        val allUsers = db.getAllUsers()
        for (user in allUsers) {
            val hash = PinHasher.hash(pin, user.salt)
            if (hash == user.pinHash) return user
        }
        return null
    }

    fun createUser(name: String, pin: String, role: UserRole) {
        val salt = PinHasher.generateSalt()
        val hash = PinHasher.hash(pin, salt)
        val id = "user_${System.currentTimeMillis()}"
        db.insertUser(UserProfile(id, name, role, hash, salt))
    }

    fun deleteUser(userId: String) = db.deleteUser(userId)
    fun getAllUsers(): List<UserProfile> = db.getAllUsers()
}

// expect/actual for System.currentTimeMillis equivalent
expect fun currentTimeMillis(): Long
```

- [ ] **Step 4: Implement AuditUseCase**

```kotlin
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
```

- [ ] **Step 5: Run tests**

```bash
cd kxkm-bmu-app && ./gradlew :shared:testDebugUnitTest 2>&1 | tail -5
```

- [ ] **Step 6: Commit**

```bash
git add shared/src/commonMain/kotlin/com/kxkm/bmu/auth/ \
        shared/src/commonMain/kotlin/com/kxkm/bmu/sync/ \
        shared/src/androidMain/kotlin/com/kxkm/bmu/auth/ \
        shared/src/iosMain/kotlin/com/kxkm/bmu/auth/ \
        shared/src/commonTest/kotlin/com/kxkm/bmu/PinHasherTest.kt
git commit -m "feat(shared): auth (PIN+roles), audit use case, platform SHA-256"
```

---

### Task 10: Domain use cases + SyncManager

**Files:**
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/domain/MonitoringUseCase.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/domain/ControlUseCase.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/domain/ConfigUseCase.kt`
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/sync/SyncManager.kt`

- [ ] **Step 1: Create MonitoringUseCase**

```kotlin
package com.kxkm.bmu.domain

import com.kxkm.bmu.db.BatteryHistoryPoint
import com.kxkm.bmu.db.DatabaseHelper
import com.kxkm.bmu.model.*
import com.kxkm.bmu.transport.TransportManager
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.datetime.Clock

class MonitoringUseCase(
    private val transport: TransportManager,
    private val db: DatabaseHelper
) {
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    /** Start recording history snapshots every 10s */
    fun startRecording() {
        scope.launch {
            transport.observeBatteries().collect { batteries ->
                batteries.forEach { db.insertHistory(it) }
                delay(10_000)
            }
        }
        // Purge old history daily
        scope.launch {
            while (true) {
                val sevenDaysAgo = Clock.System.now().toEpochMilliseconds() - 7 * 86400 * 1000L
                db.purgeOldHistory(sevenDaysAgo)
                delay(86400_000) // daily
            }
        }
    }

    fun observeBatteries(): Flow<List<BatteryState>> = transport.observeBatteries()

    fun observeBattery(index: Int): Flow<BatteryState?> =
        transport.observeBatteries().map { it.firstOrNull { b -> b.index == index } }

    fun observeSystem(): Flow<SystemInfo?> = transport.observeSystem()
    fun observeSolar(): Flow<SolarData?> = transport.observeSolar()

    fun getHistory(batteryIndex: Int, hours: Int): List<BatteryHistoryPoint> {
        val sinceMs = Clock.System.now().toEpochMilliseconds() - hours * 3600 * 1000L
        return db.getHistory(batteryIndex, sinceMs)
    }

    /** Callback-based API for iOS/Android ViewModels */
    fun observeBatteries(callback: (List<BatteryState>) -> Unit) {
        scope.launch {
            observeBatteries().collect { callback(it) }
        }
    }

    fun observeBattery(index: Int, callback: (BatteryState?) -> Unit) {
        scope.launch {
            observeBattery(index).collect { callback(it) }
        }
    }

    fun observeSystem(callback: (SystemInfo?) -> Unit) {
        scope.launch {
            observeSystem().collect { callback(it) }
        }
    }

    fun observeSolar(callback: (SolarData?) -> Unit) {
        scope.launch {
            observeSolar().collect { callback(it) }
        }
    }

    fun getHistory(batteryIndex: Int, hours: Int, callback: (List<BatteryHistoryPoint>) -> Unit) {
        scope.launch {
            callback(getHistory(batteryIndex, hours))
        }
    }
}
```

- [ ] **Step 2: Create ControlUseCase**

```kotlin
package com.kxkm.bmu.domain

import com.kxkm.bmu.model.CommandResult
import com.kxkm.bmu.sync.AuditUseCase
import com.kxkm.bmu.transport.TransportManager
import kotlinx.coroutines.*

class ControlUseCase(
    private val transport: TransportManager,
    private val audit: AuditUseCase,
    private val currentUserId: () -> String
) {
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    fun switchBattery(index: Int, on: Boolean, callback: (CommandResult) -> Unit) {
        scope.launch {
            val result = transport.switchBattery(index, on)
            if (result.isSuccess) {
                audit.record(currentUserId(), if (on) "switch_on" else "switch_off", index)
            }
            callback(result)
        }
    }

    fun resetSwitchCount(index: Int, callback: (CommandResult) -> Unit) {
        scope.launch {
            val result = transport.resetSwitchCount(index)
            if (result.isSuccess) {
                audit.record(currentUserId(), "reset", index)
            }
            callback(result)
        }
    }
}
```

- [ ] **Step 3: Create ConfigUseCase**

```kotlin
package com.kxkm.bmu.domain

import com.kxkm.bmu.model.*
import com.kxkm.bmu.sync.AuditUseCase
import com.kxkm.bmu.transport.TransportManager
import kotlinx.coroutines.*

class ConfigUseCase(
    private val transport: TransportManager,
    private val audit: AuditUseCase,
    private val currentUserId: () -> String
) {
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    fun getCurrentConfig(): ProtectionConfig {
        // Read from local cache or defaults
        return ProtectionConfig()
    }

    fun setProtectionConfig(minMv: Int, maxMv: Int, maxMa: Int, diffMv: Int,
                            callback: (CommandResult) -> Unit) {
        scope.launch {
            val config = ProtectionConfig(minMv, maxMv, maxMa, diffMv)
            val result = transport.setProtectionConfig(config)
            if (result.isSuccess) {
                audit.record(currentUserId(), "config_change", null,
                    "min=$minMv max=$maxMv maxI=$maxMa diff=$diffMv")
            }
            callback(result)
        }
    }

    fun setWifiConfig(ssid: String, password: String, callback: (CommandResult) -> Unit) {
        scope.launch {
            val result = transport.setWifiConfig(ssid, password)
            if (result.isSuccess) {
                audit.record(currentUserId(), "wifi_config", null, "ssid=$ssid")
            }
            callback(result)
        }
    }

    fun getPendingSyncCount(): Long = 0 // Delegated to SyncManager
}
```

- [ ] **Step 4: Create SyncManager**

```kotlin
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
```

- [ ] **Step 5: Build to verify**

```bash
cd kxkm-bmu-app && ./gradlew :shared:compileKotlinIosArm64 2>&1 | tail -5
```

- [ ] **Step 6: Commit**

```bash
git add shared/src/commonMain/kotlin/com/kxkm/bmu/domain/ \
        shared/src/commonMain/kotlin/com/kxkm/bmu/sync/SyncManager.kt
git commit -m "feat(shared): domain use cases (monitoring, control, config) + SyncManager"
```

---

### Task 11: SharedFactory — DI entry point

**Files:**
- Create: `shared/src/commonMain/kotlin/com/kxkm/bmu/SharedFactory.kt`

- [ ] **Step 1: Create SharedFactory**

```kotlin
package com.kxkm.bmu

import com.kxkm.bmu.auth.AuthUseCase
import com.kxkm.bmu.db.DatabaseHelper
import com.kxkm.bmu.db.DriverFactory
import com.kxkm.bmu.domain.*
import com.kxkm.bmu.sync.AuditUseCase
import com.kxkm.bmu.sync.SyncManager
import com.kxkm.bmu.transport.*

class SharedFactory(driverFactory: DriverFactory) {
    private val db = DatabaseHelper(driverFactory)
    private val ble = BleTransport()
    private val offline = OfflineTransport(db)

    private var wifi: WifiTransport? = null
    private var mqtt: MqttTransport? = null
    private var restClient: CloudRestClient? = null

    val transportManager = TransportManager(ble, wifi, mqtt, offline)
    val authUseCase = AuthUseCase(db)
    val auditUseCase = AuditUseCase(db)

    private var currentUserId: String = "unknown"

    fun setCurrentUser(userId: String) { currentUserId = userId }

    fun configureWifi(baseUrl: String, token: String) {
        wifi = WifiTransport(baseUrl, token)
    }

    fun configureCloud(apiUrl: String, apiKey: String, mqttBroker: String,
                       mqttUser: String, mqttPass: String) {
        restClient = CloudRestClient(apiUrl, apiKey)
        mqtt = MqttTransport(mqttBroker, mqttUser, mqttPass)
    }

    fun createMonitoringUseCase() = MonitoringUseCase(transportManager, db)
    fun createControlUseCase() = ControlUseCase(transportManager, auditUseCase) { currentUserId }
    fun createConfigUseCase() = ConfigUseCase(transportManager, auditUseCase) { currentUserId }
    fun createSyncManager() = SyncManager(db, restClient)

    companion object {
        // iOS needs a companion object factory since constructors with expect/actual are complex
        fun create(driverFactory: DriverFactory): SharedFactory = SharedFactory(driverFactory)
    }
}
```

- [ ] **Step 2: Build full project**

```bash
cd kxkm-bmu-app && ./gradlew :shared:build 2>&1 | tail -10
```
Expected: BUILD SUCCESSFUL, all tests pass

- [ ] **Step 3: Commit**

```bash
git add shared/src/commonMain/kotlin/com/kxkm/bmu/SharedFactory.kt
git commit -m "feat(shared): SharedFactory DI — entry point for iOS/Android apps"
```

---

## Dependencies provided to Plans 3 & 4

This module exports via `SharedFactory`:

**Use Cases:**
- `MonitoringUseCase` — `observeBatteries()`, `observeBattery(index)`, `observeSystem()`, `observeSolar()`, `getHistory(index, hours)`
- `ControlUseCase` — `switchBattery(index, on, callback)`, `resetSwitchCount(index, callback)`
- `ConfigUseCase` — `getCurrentConfig()`, `setProtectionConfig(...)`, `setWifiConfig(...)`, `getPendingSyncCount()`
- `AuthUseCase` — `authenticate(pin)`, `createUser(...)`, `deleteUser(...)`, `getAllUsers()`, `hasNoUsers()`
- `AuditUseCase` — `getEvents(...)`, `getPendingSyncCount()`
- `TransportManager` — `activeChannel`, `forcedChannel`, `start()`

**Models:** `BatteryState`, `BatteryStatus`, `SystemInfo`, `SolarData`, `AuditEvent`, `UserProfile`, `UserRole`, `TransportChannel`, `CommandResult`, `WifiStatusInfo`, `ProtectionConfig`, `BatteryHistoryPoint`
