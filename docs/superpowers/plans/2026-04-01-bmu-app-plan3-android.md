# BMU App — Plan 3: Android Jetpack Compose App

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the Android native UI for the KXKM BMU companion app using Jetpack Compose + Material3, consuming the KMP shared module for all business logic, transport, auth, and data persistence.

**Architecture:** Thin Compose layer over the KMP shared module. ViewModels wrap shared use cases and expose `StateFlow` state. Navigation via bottom `NavigationBar` (5 tabs). The shared module handles BLE, WiFi, MQTT, REST, SQLDelight, auth — Android only does UI + platform permissions.

**Tech Stack:** Jetpack Compose, Material3, Navigation Compose, Vico (charts), Hilt (DI), AndroidX BiometricPrompt

**Depends on:** Plan 2 (KMP shared module must be built first)

**Spec:** `docs/superpowers/specs/2026-04-01-smartphone-app-design.md`

---

## File Structure

```
androidApp/
├── build.gradle.kts
├── src/main/
│   ├── AndroidManifest.xml
│   ├── java/com/kxkm/bmu/
│   │   ├── BmuApplication.kt                  # Hilt Application
│   │   ├── MainActivity.kt                    # Single Activity, setContent
│   │   ├── di/
│   │   │   └── AppModule.kt                   # Hilt providers for KMP use cases
│   │   ├── viewmodel/
│   │   │   ├── AuthViewModel.kt               # PIN entry, session, role
│   │   │   ├── DashboardViewModel.kt          # Battery grid state
│   │   │   ├── BatteryDetailViewModel.kt      # Single battery + history chart
│   │   │   ├── SystemViewModel.kt             # System info + solar
│   │   │   ├── AuditViewModel.kt              # Audit trail + filters
│   │   │   └── ConfigViewModel.kt             # Config + WiFi + users
│   │   ├── ui/
│   │   │   ├── BmuNavHost.kt                  # Navigation graph
│   │   │   ├── auth/
│   │   │   │   ├── PinEntryScreen.kt          # PIN pad (6 digits)
│   │   │   │   └── OnboardingScreen.kt        # First-launch admin setup
│   │   │   ├── dashboard/
│   │   │   │   ├── DashboardScreen.kt         # Battery grid
│   │   │   │   └── BatteryCellCard.kt         # Single battery card
│   │   │   ├── detail/
│   │   │   │   ├── BatteryDetailScreen.kt     # Full detail screen
│   │   │   │   └── VoltageChart.kt            # Vico chart history
│   │   │   ├── system/
│   │   │   │   └── SystemScreen.kt            # System + solar info
│   │   │   ├── audit/
│   │   │   │   ├── AuditScreen.kt             # Event list + filters
│   │   │   │   └── AuditEventRow.kt           # Single event row
│   │   │   ├── config/
│   │   │   │   ├── ConfigScreen.kt            # Settings root
│   │   │   │   ├── ProtectionConfigScreen.kt  # V/I thresholds
│   │   │   │   ├── WifiConfigScreen.kt        # BMU WiFi setup
│   │   │   │   ├── UserManagementScreen.kt    # User CRUD
│   │   │   │   ├── SyncConfigScreen.kt        # Cloud sync settings
│   │   │   │   └── TransportConfigScreen.kt   # Channel selection
│   │   │   ├── components/
│   │   │   │   ├── StatusBar.kt               # Transport indicator
│   │   │   │   ├── BatteryStateIcon.kt        # Color-coded state icon
│   │   │   │   └── ConfirmActionDialog.kt     # Switch ON/OFF confirm
│   │   │   └── theme/
│   │   │       ├── Theme.kt                   # Material3 dynamic color
│   │   │       ├── Color.kt                   # KXKM palette
│   │   │       └── Type.kt                    # Typography
│   │   └── util/
│   │       ├── SharedBridge.kt                # KMP type extensions
│   │       └── BiometricHelper.kt             # AndroidX BiometricPrompt
│   └── res/
│       ├── values/strings.xml
│       └── mipmap-*/ (launcher icons)
```

---

### Task 1: Android project setup + KMP shared module integration

**Files:**
- Create: `androidApp/build.gradle.kts`
- Create: `androidApp/src/main/AndroidManifest.xml`
- Create: `androidApp/src/main/java/com/kxkm/bmu/BmuApplication.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/MainActivity.kt`

- [ ] **Step 1: Create Android module directory**

```bash
cd kxkm-bmu-app
mkdir -p androidApp/src/main/java/com/kxkm/bmu
mkdir -p androidApp/src/main/res/values
```

- [ ] **Step 2: Create build.gradle.kts**

Create `androidApp/build.gradle.kts`:

```kotlin
plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("com.google.dagger.hilt.android")
    id("com.google.devtools.ksp")
}

android {
    namespace = "com.kxkm.bmu"
    compileSdk = 35

    defaultConfig {
        applicationId = "com.kxkm.bmu"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0.0"
    }

    buildFeatures {
        compose = true
    }

    composeOptions {
        kotlinCompilerExtensionVersion = "1.5.14"
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

dependencies {
    implementation(project(":shared"))

    // Compose BOM
    val composeBom = platform("androidx.compose:compose-bom:2025.01.01")
    implementation(composeBom)
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-tooling-preview")
    debugImplementation("androidx.compose.ui:ui-tooling")

    // Navigation Compose
    implementation("androidx.navigation:navigation-compose:2.8.5")

    // Lifecycle + ViewModel
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")

    // Hilt
    implementation("com.google.dagger:hilt-android:2.52")
    ksp("com.google.dagger:hilt-compiler:2.52")
    implementation("androidx.hilt:hilt-navigation-compose:1.2.0")

    // Vico charts
    implementation("com.patrykandpatrick.vico:compose-m3:2.0.1")

    // Biometric
    implementation("androidx.biometric:biometric:1.1.0")

    // Activity Compose
    implementation("androidx.activity:activity-compose:1.9.3")
}
```

- [ ] **Step 3: Add to root settings.gradle.kts**

Ensure `settings.gradle.kts` includes:

```kotlin
include(":androidApp")
include(":shared")
```

- [ ] **Step 4: Create AndroidManifest.xml**

Create `androidApp/src/main/AndroidManifest.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <!-- BLE permissions -->
    <uses-permission android:name="android.permission.BLUETOOTH" />
    <uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
    <uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
    <uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
    <uses-permission android:name="android.permission.ACCESS_FINE_LOCATION" />

    <!-- WiFi / network -->
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
    <uses-permission android:name="android.permission.ACCESS_WIFI_STATE" />

    <!-- Biometric -->
    <uses-permission android:name="android.permission.USE_BIOMETRIC" />

    <uses-feature android:name="android.hardware.bluetooth_le" android:required="true" />

    <application
        android:name=".BmuApplication"
        android:label="KXKM BMU"
        android:icon="@mipmap/ic_launcher"
        android:theme="@style/Theme.Material3.DayNight.NoActionBar"
        android:supportsRtl="true">

        <activity
            android:name=".MainActivity"
            android:exported="true"
            android:windowSoftInputMode="adjustResize">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>
</manifest>
```

- [ ] **Step 5: Create BmuApplication**

Create `androidApp/src/main/java/com/kxkm/bmu/BmuApplication.kt`:

```kotlin
package com.kxkm.bmu

import android.app.Application
import dagger.hilt.android.HiltAndroidApp

@HiltAndroidApp
class BmuApplication : Application()
```

- [ ] **Step 6: Create MainActivity**

Create `androidApp/src/main/java/com/kxkm/bmu/MainActivity.kt`:

```kotlin
package com.kxkm.bmu

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.ui.BmuNavHost
import com.kxkm.bmu.ui.auth.OnboardingScreen
import com.kxkm.bmu.ui.auth.PinEntryScreen
import com.kxkm.bmu.ui.theme.BmuTheme
import com.kxkm.bmu.viewmodel.AuthViewModel
import dagger.hilt.android.AndroidEntryPoint

@AndroidEntryPoint
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            BmuTheme {
                val authVM: AuthViewModel = hiltViewModel()
                val isAuthenticated by authVM.isAuthenticated.collectAsState()
                val needsOnboarding by authVM.needsOnboarding.collectAsState()

                when {
                    needsOnboarding -> OnboardingScreen(authVM = authVM)
                    !isAuthenticated -> PinEntryScreen(authVM = authVM)
                    else -> BmuNavHost(authVM = authVM)
                }
            }
        }
    }
}
```

- [ ] **Step 7: Create strings.xml**

Create `androidApp/src/main/res/values/strings.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="app_name">KXKM BMU</string>
    <string name="tab_batteries">Batteries</string>
    <string name="tab_system">Système</string>
    <string name="tab_audit">Audit</string>
    <string name="tab_config">Config</string>
    <string name="ble_permission_rationale">KXKM BMU utilise le Bluetooth pour communiquer avec le boîtier de gestion batteries.</string>
    <string name="biometric_prompt_title">Déverrouiller KXKM BMU</string>
    <string name="biometric_prompt_subtitle">Utilisez votre empreinte ou votre visage</string>
</resources>
```

- [ ] **Step 8: Build to verify**

```bash
cd kxkm-bmu-app && ./gradlew :androidApp:assembleDebug 2>&1 | tail -5
```
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 9: Commit**

```bash
git add androidApp/
git commit -m "feat(android): project setup with Compose, Hilt, KMP shared dependency"
```

---

### Task 2: Theme + KMP bridge helpers

**Files:**
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/theme/Color.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/theme/Type.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/theme/Theme.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/util/SharedBridge.kt`

- [ ] **Step 1: Create Color.kt**

```kotlin
package com.kxkm.bmu.ui.theme

import androidx.compose.ui.graphics.Color

// KXKM brand palette
val KxkmGreen = Color(0xFF4CAF50)
val KxkmRed = Color(0xFFF44336)
val KxkmOrange = Color(0xFFFF9800)
val KxkmYellow = Color(0xFFFFEB3B)
val KxkmBlue = Color(0xFF2196F3)

// Battery state colors
val BatteryConnected = Color(0xFF4CAF50)
val BatteryDisconnected = Color(0xFFF44336)
val BatteryReconnecting = Color(0xFFFFEB3B)
val BatteryError = Color(0xFFFF9800)
val BatteryLocked = Color(0xFFF44336)
```

- [ ] **Step 2: Create Type.kt**

```kotlin
package com.kxkm.bmu.ui.theme

import androidx.compose.material3.Typography
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp

val BmuTypography = Typography(
    headlineLarge = TextStyle(
        fontFamily = FontFamily.Default,
        fontWeight = FontWeight.Bold,
        fontSize = 28.sp,
        lineHeight = 36.sp,
    ),
    titleLarge = TextStyle(
        fontFamily = FontFamily.Default,
        fontWeight = FontWeight.Bold,
        fontSize = 22.sp,
        lineHeight = 28.sp,
    ),
    bodyLarge = TextStyle(
        fontFamily = FontFamily.Default,
        fontWeight = FontWeight.Normal,
        fontSize = 16.sp,
        lineHeight = 24.sp,
    ),
    labelSmall = TextStyle(
        fontFamily = FontFamily.Default,
        fontWeight = FontWeight.Medium,
        fontSize = 11.sp,
        lineHeight = 16.sp,
    ),
)
```

- [ ] **Step 3: Create Theme.kt**

```kotlin
package com.kxkm.bmu.ui.theme

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext

private val DarkColorScheme = darkColorScheme(
    primary = KxkmGreen,
    secondary = KxkmBlue,
    error = KxkmRed,
)

private val LightColorScheme = lightColorScheme(
    primary = KxkmGreen,
    secondary = KxkmBlue,
    error = KxkmRed,
)

@Composable
fun BmuTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    dynamicColor: Boolean = true,
    content: @Composable () -> Unit,
) {
    val colorScheme = when {
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
            val context = LocalContext.current
            if (darkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        }
        darkTheme -> DarkColorScheme
        else -> LightColorScheme
    }

    MaterialTheme(
        colorScheme = colorScheme,
        typography = BmuTypography,
        content = content,
    )
}
```

- [ ] **Step 4: Create SharedBridge.kt**

```kotlin
package com.kxkm.bmu.util

import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.Cloud
import androidx.compose.material.icons.filled.CloudOff
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.QuestionMark
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material.icons.outlined.BoltOutlined
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import com.kxkm.bmu.shared.model.BatteryStatus
import com.kxkm.bmu.shared.model.TransportChannel
import com.kxkm.bmu.shared.model.UserRole
import com.kxkm.bmu.ui.theme.BatteryConnected
import com.kxkm.bmu.ui.theme.BatteryDisconnected
import com.kxkm.bmu.ui.theme.BatteryError
import com.kxkm.bmu.ui.theme.BatteryLocked
import com.kxkm.bmu.ui.theme.BatteryReconnecting

// MARK: - BatteryStatus extensions

val BatteryStatus.displayName: String
    get() = when (this) {
        BatteryStatus.CONNECTED -> "Connecté"
        BatteryStatus.DISCONNECTED -> "Déconnecté"
        BatteryStatus.RECONNECTING -> "Reconnexion"
        BatteryStatus.ERROR -> "Erreur"
        BatteryStatus.LOCKED -> "Verrouillé"
    }

val BatteryStatus.color: Color
    get() = when (this) {
        BatteryStatus.CONNECTED -> BatteryConnected
        BatteryStatus.DISCONNECTED -> BatteryDisconnected
        BatteryStatus.RECONNECTING -> BatteryReconnecting
        BatteryStatus.ERROR -> BatteryError
        BatteryStatus.LOCKED -> BatteryLocked
    }

val BatteryStatus.icon: ImageVector
    get() = when (this) {
        BatteryStatus.CONNECTED -> Icons.Filled.Bolt
        BatteryStatus.DISCONNECTED -> Icons.Outlined.BoltOutlined
        BatteryStatus.RECONNECTING -> Icons.Filled.Refresh
        BatteryStatus.ERROR -> Icons.Filled.Warning
        BatteryStatus.LOCKED -> Icons.Filled.Lock
    }

// MARK: - UserRole extensions

val UserRole.displayName: String
    get() = when (this) {
        UserRole.ADMIN -> "Admin"
        UserRole.TECHNICIAN -> "Technicien"
        UserRole.VIEWER -> "Lecteur"
    }

val UserRole.canControl: Boolean
    get() = this == UserRole.ADMIN || this == UserRole.TECHNICIAN

val UserRole.canConfigure: Boolean
    get() = this == UserRole.ADMIN

// MARK: - TransportChannel extensions

val TransportChannel.displayName: String
    get() = when (this) {
        TransportChannel.BLE -> "BLE"
        TransportChannel.WIFI -> "WiFi"
        TransportChannel.MQTT_CLOUD -> "Cloud MQTT"
        TransportChannel.REST_CLOUD -> "Cloud REST"
        TransportChannel.OFFLINE -> "Hors ligne"
    }

val TransportChannel.icon: ImageVector
    get() = when (this) {
        TransportChannel.BLE -> Icons.Filled.Bolt
        TransportChannel.WIFI -> Icons.Filled.Wifi
        TransportChannel.MQTT_CLOUD, TransportChannel.REST_CLOUD -> Icons.Filled.Cloud
        TransportChannel.OFFLINE -> Icons.Filled.CloudOff
    }

// MARK: - Unit formatting

fun Int.voltageDisplay(): String = "%.2f V".format(this / 1000.0)

fun Int.currentDisplay(): String = "%.2f A".format(this / 1000.0)

fun Int.ahDisplay(): String = "%.2f Ah".format(this / 1000.0)

fun Long.formatUptime(): String {
    val h = this / 3600
    val m = (this % 3600) / 60
    return "${h}h ${m}m"
}

fun Long.formatBytes(): String {
    return if (this > 1_000_000) "%.1f MB".format(this / 1_000_000.0)
    else "%.0f KB".format(this / 1000.0)
}
```

- [ ] **Step 5: Build to verify**

```bash
./gradlew :androidApp:assembleDebug 2>&1 | tail -3
```

- [ ] **Step 6: Commit**

```bash
git add androidApp/src/main/java/com/kxkm/bmu/ui/theme/ \
        androidApp/src/main/java/com/kxkm/bmu/util/SharedBridge.kt
git commit -m "feat(android): Material3 theme + KMP bridge helpers"
```

---

### Task 3: Hilt DI module

**Files:**
- Create: `androidApp/src/main/java/com/kxkm/bmu/di/AppModule.kt`

- [ ] **Step 1: Create AppModule**

```kotlin
package com.kxkm.bmu.di

import com.kxkm.bmu.shared.SharedFactory
import com.kxkm.bmu.shared.auth.AuthUseCase
import com.kxkm.bmu.shared.domain.AuditUseCase
import com.kxkm.bmu.shared.domain.ConfigUseCase
import com.kxkm.bmu.shared.domain.ControlUseCase
import com.kxkm.bmu.shared.domain.MonitoringUseCase
import com.kxkm.bmu.shared.transport.TransportManager
import dagger.Module
import dagger.Provides
import dagger.hilt.InstallIn
import dagger.hilt.components.SingletonComponent
import javax.inject.Singleton

@Module
@InstallIn(SingletonComponent::class)
object AppModule {

    @Provides
    @Singleton
    fun provideTransportManager(): TransportManager =
        SharedFactory.createTransportManager()

    @Provides
    @Singleton
    fun provideMonitoringUseCase(): MonitoringUseCase =
        SharedFactory.createMonitoringUseCase()

    @Provides
    @Singleton
    fun provideControlUseCase(): ControlUseCase =
        SharedFactory.createControlUseCase()

    @Provides
    @Singleton
    fun provideConfigUseCase(): ConfigUseCase =
        SharedFactory.createConfigUseCase()

    @Provides
    @Singleton
    fun provideAuditUseCase(): AuditUseCase =
        SharedFactory.createAuditUseCase()

    @Provides
    @Singleton
    fun provideAuthUseCase(): AuthUseCase =
        SharedFactory.createAuthUseCase()
}
```

- [ ] **Step 2: Build to verify**

```bash
./gradlew :androidApp:assembleDebug 2>&1 | tail -3
```

- [ ] **Step 3: Commit**

```bash
git add androidApp/src/main/java/com/kxkm/bmu/di/
git commit -m "feat(android): Hilt DI module providing KMP shared use cases"
```

---

### Task 4: Auth — PIN entry + onboarding + biometric

**Files:**
- Create: `androidApp/src/main/java/com/kxkm/bmu/viewmodel/AuthViewModel.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/auth/PinEntryScreen.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/auth/OnboardingScreen.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/util/BiometricHelper.kt`

- [ ] **Step 1: Create AuthViewModel**

```kotlin
package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import com.kxkm.bmu.shared.auth.AuthUseCase
import com.kxkm.bmu.shared.model.UserProfile
import com.kxkm.bmu.shared.model.UserRole
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import javax.inject.Inject

@HiltViewModel
class AuthViewModel @Inject constructor(
    private val authUseCase: AuthUseCase,
) : ViewModel() {

    private val _isAuthenticated = MutableStateFlow(false)
    val isAuthenticated: StateFlow<Boolean> = _isAuthenticated.asStateFlow()

    private val _needsOnboarding = MutableStateFlow(authUseCase.hasNoUsers())
    val needsOnboarding: StateFlow<Boolean> = _needsOnboarding.asStateFlow()

    private val _currentUser = MutableStateFlow<UserProfile?>(null)
    val currentUser: StateFlow<UserProfile?> = _currentUser.asStateFlow()

    private val _pinError = MutableStateFlow<String?>(null)
    val pinError: StateFlow<String?> = _pinError.asStateFlow()

    fun login(pin: String) {
        val user = authUseCase.authenticate(pin)
        if (user != null) {
            _currentUser.value = user
            _isAuthenticated.value = true
            _pinError.value = null
        } else {
            _pinError.value = "PIN incorrect"
        }
    }

    fun createAdmin(name: String, pin: String) {
        authUseCase.createUser(name, pin, UserRole.ADMIN)
        _needsOnboarding.value = false
    }

    fun logout() {
        _isAuthenticated.value = false
        _currentUser.value = null
    }
}
```

- [ ] **Step 2: Create PinEntryScreen**

```kotlin
package com.kxkm.bmu.ui.auth

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Backspace
import androidx.compose.material.icons.filled.Fingerprint
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.kxkm.bmu.util.BiometricHelper
import com.kxkm.bmu.viewmodel.AuthViewModel

@Composable
fun PinEntryScreen(authVM: AuthViewModel) {
    var pin by remember { mutableStateOf("") }
    val pinError by authVM.pinError.collectAsState()
    val isAuthenticated by authVM.isAuthenticated.collectAsState()
    val context = LocalContext.current

    // Auto-submit when 6 digits entered
    LaunchedEffect(pin) {
        if (pin.length == 6) {
            authVM.login(pin)
            // If login failed, clear pin
            if (!isAuthenticated) pin = ""
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Icon(
            imageVector = Icons.Filled.Fingerprint,
            contentDescription = null,
            modifier = Modifier.size(64.dp),
            tint = MaterialTheme.colorScheme.primary,
        )

        Spacer(modifier = Modifier.height(16.dp))

        Text(
            text = "KXKM BMU",
            style = MaterialTheme.typography.headlineLarge,
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = "Entrez votre PIN",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        Spacer(modifier = Modifier.height(24.dp))

        // PIN dots
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            repeat(6) { i ->
                Box(
                    modifier = Modifier
                        .size(16.dp)
                        .clip(CircleShape)
                        .background(
                            if (i < pin.length) MaterialTheme.colorScheme.primary
                            else MaterialTheme.colorScheme.outlineVariant
                        ),
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Error message
        if (pinError != null) {
            Text(
                text = pinError ?: "",
                color = MaterialTheme.colorScheme.error,
                style = MaterialTheme.typography.bodySmall,
            )
        }

        Spacer(modifier = Modifier.height(32.dp))

        // Number pad
        LazyVerticalGrid(
            columns = GridCells.Fixed(3),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
            modifier = Modifier.padding(horizontal = 40.dp),
        ) {
            // 1-9
            items(9) { index ->
                val num = index + 1
                PinPadButton(label = "$num") {
                    if (pin.length < 6) pin += "$num"
                }
            }
            // Biometric
            item {
                PinPadButton(
                    icon = {
                        Icon(
                            Icons.Filled.Fingerprint,
                            contentDescription = "Biométrie",
                        )
                    },
                ) {
                    BiometricHelper.authenticate(context) { success ->
                        if (success) {
                            val storedPin = BiometricHelper.getStoredPin(context)
                            if (storedPin != null) authVM.login(storedPin)
                        }
                    }
                }
            }
            // 0
            item {
                PinPadButton(label = "0") {
                    if (pin.length < 6) pin += "0"
                }
            }
            // Backspace
            item {
                PinPadButton(
                    icon = {
                        Icon(
                            Icons.AutoMirrored.Filled.Backspace,
                            contentDescription = "Effacer",
                        )
                    },
                ) {
                    if (pin.isNotEmpty()) pin = pin.dropLast(1)
                }
            }
        }
    }
}

@Composable
private fun PinPadButton(
    label: String? = null,
    icon: @Composable (() -> Unit)? = null,
    onClick: () -> Unit,
) {
    FilledTonalButton(
        onClick = onClick,
        modifier = Modifier.size(72.dp),
        shape = CircleShape,
    ) {
        if (icon != null) {
            icon()
        } else {
            Text(
                text = label ?: "",
                style = MaterialTheme.typography.titleLarge,
            )
        }
    }
}
```

- [ ] **Step 3: Create OnboardingScreen**

```kotlin
package com.kxkm.bmu.ui.auth

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BatteryChargingFull
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.kxkm.bmu.viewmodel.AuthViewModel

@Composable
fun OnboardingScreen(authVM: AuthViewModel) {
    var name by remember { mutableStateOf("") }
    var pin by remember { mutableStateOf("") }
    var confirmPin by remember { mutableStateOf("") }
    var step by remember { mutableIntStateOf(0) }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Icon(
            imageVector = Icons.Filled.BatteryChargingFull,
            contentDescription = null,
            modifier = Modifier.size(64.dp),
            tint = MaterialTheme.colorScheme.primary,
        )

        Spacer(modifier = Modifier.height(16.dp))

        Text(
            text = "Configuration initiale",
            style = MaterialTheme.typography.titleLarge,
        )

        Spacer(modifier = Modifier.height(32.dp))

        when (step) {
            0 -> {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    label = { Text("Votre nom") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )

                Spacer(modifier = Modifier.height(16.dp))

                Button(
                    onClick = { step = 1 },
                    enabled = name.isNotBlank(),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Suivant")
                }
            }
            1 -> {
                Text(
                    text = "Choisissez un PIN (6 chiffres)",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )

                Spacer(modifier = Modifier.height(8.dp))

                OutlinedTextField(
                    value = pin,
                    onValueChange = { if (it.length <= 6 && it.all(Char::isDigit)) pin = it },
                    label = { Text("PIN") },
                    singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                    modifier = Modifier.fillMaxWidth(),
                )

                Spacer(modifier = Modifier.height(16.dp))

                Button(
                    onClick = { step = 2 },
                    enabled = pin.length == 6,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Suivant")
                }
            }
            2 -> {
                Text(
                    text = "Confirmez le PIN",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )

                Spacer(modifier = Modifier.height(8.dp))

                OutlinedTextField(
                    value = confirmPin,
                    onValueChange = { if (it.length <= 6 && it.all(Char::isDigit)) confirmPin = it },
                    label = { Text("PIN") },
                    singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                    modifier = Modifier.fillMaxWidth(),
                )

                if (confirmPin.length == 6 && confirmPin != pin) {
                    Spacer(modifier = Modifier.height(4.dp))
                    Text(
                        text = "Les PINs ne correspondent pas",
                        color = MaterialTheme.colorScheme.error,
                        style = MaterialTheme.typography.bodySmall,
                    )
                }

                Spacer(modifier = Modifier.height(16.dp))

                Button(
                    onClick = {
                        authVM.createAdmin(name, pin)
                        authVM.login(pin)
                    },
                    enabled = confirmPin == pin && pin.length == 6,
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Text("Créer le compte Admin")
                }
            }
        }
    }
}
```

- [ ] **Step 4: Create BiometricHelper**

```kotlin
package com.kxkm.bmu.util

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import androidx.fragment.app.FragmentActivity
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey

object BiometricHelper {

    private const val PREFS_NAME = "bmu_biometric"
    private const val KEY_PIN = "stored_pin"

    fun authenticate(context: Context, onResult: (Boolean) -> Unit) {
        val activity = context as? FragmentActivity ?: run {
            onResult(false)
            return
        }

        val biometricManager = BiometricManager.from(context)
        if (biometricManager.canAuthenticate(BiometricManager.Authenticators.BIOMETRIC_STRONG)
            != BiometricManager.BIOMETRIC_SUCCESS
        ) {
            onResult(false)
            return
        }

        val executor = ContextCompat.getMainExecutor(context)
        val callback = object : BiometricPrompt.AuthenticationCallback() {
            override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                onResult(true)
            }

            override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                onResult(false)
            }

            override fun onAuthenticationFailed() {
                // Single attempt failed — prompt stays open, do nothing
            }
        }

        val prompt = BiometricPrompt(activity, executor, callback)
        val promptInfo = BiometricPrompt.PromptInfo.Builder()
            .setTitle("Déverrouiller KXKM BMU")
            .setSubtitle("Utilisez votre empreinte ou votre visage")
            .setNegativeButtonText("Annuler")
            .build()

        prompt.authenticate(promptInfo)
    }

    fun storePin(context: Context, pin: String) {
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_PIN, pin)
            .apply()
    }

    fun getStoredPin(context: Context): String? {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .getString(KEY_PIN, null)
    }
}
```

- [ ] **Step 5: Build to verify**

```bash
./gradlew :androidApp:assembleDebug 2>&1 | tail -3
```

- [ ] **Step 6: Commit**

```bash
git add androidApp/src/main/java/com/kxkm/bmu/viewmodel/AuthViewModel.kt \
        androidApp/src/main/java/com/kxkm/bmu/ui/auth/ \
        androidApp/src/main/java/com/kxkm/bmu/util/BiometricHelper.kt
git commit -m "feat(android): PIN entry, onboarding, biometric auth"
```

---

### Task 5: Navigation — bottom bar + NavHost

**Files:**
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/BmuNavHost.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/components/StatusBar.kt`

- [ ] **Step 1: Create BmuNavHost**

```kotlin
package com.kxkm.bmu.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.ListAlt
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.kxkm.bmu.ui.audit.AuditScreen
import com.kxkm.bmu.ui.components.StatusBar
import com.kxkm.bmu.ui.config.ConfigScreen
import com.kxkm.bmu.ui.config.ProtectionConfigScreen
import com.kxkm.bmu.ui.config.SyncConfigScreen
import com.kxkm.bmu.ui.config.TransportConfigScreen
import com.kxkm.bmu.ui.config.UserManagementScreen
import com.kxkm.bmu.ui.config.WifiConfigScreen
import com.kxkm.bmu.ui.dashboard.DashboardScreen
import com.kxkm.bmu.ui.detail.BatteryDetailScreen
import com.kxkm.bmu.ui.system.SystemScreen
import com.kxkm.bmu.util.canConfigure
import com.kxkm.bmu.viewmodel.AuthViewModel

sealed class BmuRoute(val route: String, val label: String, val icon: ImageVector) {
    data object Dashboard : BmuRoute("dashboard", "Batteries", Icons.Filled.Bolt)
    data object System : BmuRoute("system", "Système", Icons.Filled.Memory)
    data object Audit : BmuRoute("audit", "Audit", Icons.Filled.ListAlt)
    data object Config : BmuRoute("config", "Config", Icons.Filled.Settings)
}

@Composable
fun BmuNavHost(authVM: AuthViewModel) {
    val navController = rememberNavController()
    val currentUser by authVM.currentUser.collectAsState()
    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = navBackStackEntry?.destination?.route

    val tabs = buildList {
        add(BmuRoute.Dashboard)
        add(BmuRoute.System)
        add(BmuRoute.Audit)
        if (currentUser?.role?.canConfigure == true) {
            add(BmuRoute.Config)
        }
    }

    Scaffold(
        topBar = { StatusBar() },
        bottomBar = {
            NavigationBar {
                tabs.forEach { tab ->
                    NavigationBarItem(
                        icon = { Icon(tab.icon, contentDescription = tab.label) },
                        label = { Text(tab.label) },
                        selected = currentRoute == tab.route,
                        onClick = {
                            navController.navigate(tab.route) {
                                popUpTo(navController.graph.findStartDestination().id) {
                                    saveState = true
                                }
                                launchSingleTop = true
                                restoreState = true
                            }
                        },
                    )
                }
            }
        },
    ) { innerPadding ->
        NavHost(
            navController = navController,
            startDestination = BmuRoute.Dashboard.route,
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding),
        ) {
            composable(BmuRoute.Dashboard.route) {
                DashboardScreen(
                    onBatteryClick = { index ->
                        navController.navigate("battery_detail/$index")
                    },
                )
            }

            composable(
                route = "battery_detail/{batteryIndex}",
                arguments = listOf(navArgument("batteryIndex") { type = NavType.IntType }),
            ) { backStackEntry ->
                val batteryIndex = backStackEntry.arguments?.getInt("batteryIndex") ?: 0
                BatteryDetailScreen(
                    batteryIndex = batteryIndex,
                    authVM = authVM,
                )
            }

            composable(BmuRoute.System.route) {
                SystemScreen()
            }

            composable(BmuRoute.Audit.route) {
                AuditScreen()
            }

            composable(BmuRoute.Config.route) {
                ConfigScreen(
                    onNavigateProtection = { navController.navigate("config/protection") },
                    onNavigateWifi = { navController.navigate("config/wifi") },
                    onNavigateUsers = { navController.navigate("config/users") },
                    onNavigateSync = { navController.navigate("config/sync") },
                    onNavigateTransport = { navController.navigate("config/transport") },
                )
            }

            composable("config/protection") { ProtectionConfigScreen() }
            composable("config/wifi") { WifiConfigScreen() }
            composable("config/users") { UserManagementScreen() }
            composable("config/sync") { SyncConfigScreen() }
            composable("config/transport") { TransportConfigScreen() }
        }
    }
}
```

- [ ] **Step 2: Create StatusBar**

```kotlin
package com.kxkm.bmu.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.util.displayName
import com.kxkm.bmu.util.icon
import com.kxkm.bmu.viewmodel.TransportStatusViewModel

@Composable
fun StatusBar(
    viewModel: TransportStatusViewModel = hiltViewModel(),
) {
    val channel by viewModel.channel.collectAsState()
    val isConnected by viewModel.isConnected.collectAsState()
    val deviceName by viewModel.deviceName.collectAsState()
    val rssi by viewModel.rssi.collectAsState()

    Row(
        modifier = Modifier
            .fillMaxWidth()
            .background(MaterialTheme.colorScheme.surfaceVariant)
            .padding(horizontal = 16.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Icon(
            imageVector = channel.icon,
            contentDescription = null,
            tint = if (isConnected) MaterialTheme.colorScheme.primary
            else MaterialTheme.colorScheme.error,
        )

        Text(
            text = channel.displayName,
            style = MaterialTheme.typography.labelMedium,
            modifier = Modifier.padding(start = 6.dp),
        )

        Spacer(modifier = Modifier.weight(1f))

        deviceName?.let {
            Text(
                text = it,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        rssi?.let {
            Text(
                text = "$it dBm",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(start = 8.dp),
            )
        }
    }
}
```

- [ ] **Step 3: Create TransportStatusViewModel**

Create `androidApp/src/main/java/com/kxkm/bmu/viewmodel/TransportStatusViewModel.kt`:

```kotlin
package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import com.kxkm.bmu.shared.model.TransportChannel
import com.kxkm.bmu.shared.transport.TransportManager
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import javax.inject.Inject

@HiltViewModel
class TransportStatusViewModel @Inject constructor(
    private val transportManager: TransportManager,
) : ViewModel() {

    // These StateFlows will be wired to TransportManager's flows
    // when Plan 2 defines the exact Flow API
    val channel: StateFlow<TransportChannel> = MutableStateFlow(TransportChannel.OFFLINE).asStateFlow()
    val isConnected: StateFlow<Boolean> = MutableStateFlow(false).asStateFlow()
    val deviceName: StateFlow<String?> = MutableStateFlow<String?>(null).asStateFlow()
    val rssi: StateFlow<Int?> = MutableStateFlow<Int?>(null).asStateFlow()
}
```

- [ ] **Step 4: Build to verify**

```bash
./gradlew :androidApp:assembleDebug 2>&1 | tail -3
```

- [ ] **Step 5: Commit**

```bash
git add androidApp/src/main/java/com/kxkm/bmu/ui/BmuNavHost.kt \
        androidApp/src/main/java/com/kxkm/bmu/ui/components/StatusBar.kt \
        androidApp/src/main/java/com/kxkm/bmu/viewmodel/TransportStatusViewModel.kt
git commit -m "feat(android): Navigation Compose bottom bar + transport status bar"
```

---

### Task 6: Dashboard — battery grid

**Files:**
- Create: `androidApp/src/main/java/com/kxkm/bmu/viewmodel/DashboardViewModel.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/dashboard/DashboardScreen.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/dashboard/BatteryCellCard.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/components/BatteryStateIcon.kt`

- [ ] **Step 1: Create DashboardViewModel**

```kotlin
package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kxkm.bmu.shared.domain.MonitoringUseCase
import com.kxkm.bmu.shared.model.BatteryState
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class DashboardViewModel @Inject constructor(
    private val monitoringUseCase: MonitoringUseCase,
) : ViewModel() {

    private val _batteries = MutableStateFlow<List<BatteryState>>(emptyList())
    val batteries: StateFlow<List<BatteryState>> = _batteries.asStateFlow()

    private val _isLoading = MutableStateFlow(true)
    val isLoading: StateFlow<Boolean> = _isLoading.asStateFlow()

    init {
        viewModelScope.launch {
            // Collect KMP StateFlow<List<BatteryState>>
            monitoringUseCase.observeBatteries { states ->
                _batteries.value = states
                _isLoading.value = false
            }
        }
    }
}
```

- [ ] **Step 2: Create BatteryStateIcon**

```kotlin
package com.kxkm.bmu.ui.components

import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.kxkm.bmu.shared.model.BatteryStatus
import com.kxkm.bmu.util.color
import com.kxkm.bmu.util.displayName
import com.kxkm.bmu.util.icon

@Composable
fun BatteryStateIcon(
    state: BatteryStatus,
    modifier: Modifier = Modifier,
) {
    Icon(
        imageVector = state.icon,
        contentDescription = state.displayName,
        tint = state.color,
        modifier = modifier,
    )
}
```

- [ ] **Step 3: Create BatteryCellCard**

```kotlin
package com.kxkm.bmu.ui.dashboard

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.SwapVert
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.kxkm.bmu.shared.model.BatteryState
import com.kxkm.bmu.ui.components.BatteryStateIcon
import com.kxkm.bmu.util.color
import com.kxkm.bmu.util.currentDisplay
import com.kxkm.bmu.util.voltageDisplay

@Composable
fun BatteryCellCard(
    battery: BatteryState,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Card(
        onClick = onClick,
        modifier = modifier,
        border = BorderStroke(2.dp, battery.state.color.copy(alpha = 0.5f)),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = "Bat ${battery.index + 1}",
                    style = MaterialTheme.typography.labelMedium,
                )
                BatteryStateIcon(state = battery.state)
            }

            Text(
                text = battery.voltageMv.voltageDisplay(),
                style = MaterialTheme.typography.titleLarge,
                fontFamily = FontFamily.Monospace,
                color = voltageColor(battery.voltageMv),
            )

            Text(
                text = battery.currentMa.currentDisplay(),
                style = MaterialTheme.typography.bodySmall,
                fontFamily = FontFamily.Monospace,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                Icon(
                    imageVector = Icons.Filled.SwapVert,
                    contentDescription = "Switches",
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(0.dp),
                )
                Text(
                    text = "${battery.nbSwitch}",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun voltageColor(mv: Int) = when {
    mv < 24000 || mv > 30000 -> MaterialTheme.colorScheme.error
    mv < 24500 || mv > 29500 -> MaterialTheme.colorScheme.tertiary
    else -> MaterialTheme.colorScheme.onSurface
}
```

- [ ] **Step 4: Create DashboardScreen**

```kotlin
package com.kxkm.bmu.ui.dashboard

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BoltOutlined
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.viewmodel.DashboardViewModel

@Composable
fun DashboardScreen(
    onBatteryClick: (Int) -> Unit,
    viewModel: DashboardViewModel = hiltViewModel(),
) {
    val batteries by viewModel.batteries.collectAsState()
    val isLoading by viewModel.isLoading.collectAsState()

    when {
        isLoading -> {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center,
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    CircularProgressIndicator()
                    Spacer(modifier = Modifier.height(12.dp))
                    Text("Connexion au BMU...")
                }
            }
        }
        batteries.isEmpty() -> {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center,
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Icon(
                        imageVector = Icons.Filled.BoltOutlined,
                        contentDescription = null,
                        modifier = Modifier.size(48.dp),
                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Text(
                        text = "Aucune batterie détectée",
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
        else -> {
            LazyVerticalGrid(
                columns = GridCells.Adaptive(minSize = 140.dp),
                contentPadding = PaddingValues(12.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                items(batteries, key = { it.index }) { battery ->
                    BatteryCellCard(
                        battery = battery,
                        onClick = { onBatteryClick(battery.index) },
                    )
                }
            }
        }
    }
}
```

- [ ] **Step 5: Build to verify**

```bash
./gradlew :androidApp:assembleDebug 2>&1 | tail -3
```

- [ ] **Step 6: Commit**

```bash
git add androidApp/src/main/java/com/kxkm/bmu/viewmodel/DashboardViewModel.kt \
        androidApp/src/main/java/com/kxkm/bmu/ui/dashboard/ \
        androidApp/src/main/java/com/kxkm/bmu/ui/components/BatteryStateIcon.kt
git commit -m "feat(android): dashboard battery grid with adaptive layout"
```

---

### Task 7: Battery detail — info + chart + controls

**Files:**
- Create: `androidApp/src/main/java/com/kxkm/bmu/viewmodel/BatteryDetailViewModel.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/detail/BatteryDetailScreen.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/detail/VoltageChart.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/components/ConfirmActionDialog.kt`

- [ ] **Step 1: Create BatteryDetailViewModel**

```kotlin
package com.kxkm.bmu.viewmodel

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kxkm.bmu.shared.domain.ControlUseCase
import com.kxkm.bmu.shared.domain.MonitoringUseCase
import com.kxkm.bmu.shared.model.BatteryHistoryPoint
import com.kxkm.bmu.shared.model.BatteryState
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class BatteryDetailViewModel @Inject constructor(
    savedStateHandle: SavedStateHandle,
    private val monitoringUseCase: MonitoringUseCase,
    private val controlUseCase: ControlUseCase,
) : ViewModel() {

    val batteryIndex: Int = savedStateHandle.get<Int>("batteryIndex") ?: 0

    private val _battery = MutableStateFlow<BatteryState?>(null)
    val battery: StateFlow<BatteryState?> = _battery.asStateFlow()

    private val _history = MutableStateFlow<List<BatteryHistoryPoint>>(emptyList())
    val history: StateFlow<List<BatteryHistoryPoint>> = _history.asStateFlow()

    private val _commandResult = MutableStateFlow<String?>(null)
    val commandResult: StateFlow<String?> = _commandResult.asStateFlow()

    init {
        viewModelScope.launch {
            monitoringUseCase.observeBattery(batteryIndex) { state ->
                _battery.value = state
            }
        }
        loadHistory()
    }

    private fun loadHistory() {
        viewModelScope.launch {
            monitoringUseCase.getHistory(batteryIndex, hours = 24) { points ->
                _history.value = points
            }
        }
    }

    fun switchBattery(on: Boolean) {
        viewModelScope.launch {
            controlUseCase.switchBattery(batteryIndex, on) { result ->
                _commandResult.value =
                    if (result.isSuccess) "OK" else "Erreur: ${result.errorMessage ?: ""}"
            }
        }
    }

    fun resetSwitchCount() {
        viewModelScope.launch {
            controlUseCase.resetSwitchCount(batteryIndex) { result ->
                _commandResult.value =
                    if (result.isSuccess) "Compteur remis à zéro" else "Erreur"
            }
        }
    }
}
```

- [ ] **Step 2: Create VoltageChart**

```kotlin
package com.kxkm.bmu.ui.detail

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.kxkm.bmu.shared.model.BatteryHistoryPoint
import com.patrykandpatrick.vico.compose.cartesian.CartesianChartHost
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberBottomAxis
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberStartAxis
import com.patrykandpatrick.vico.compose.cartesian.layer.rememberLineCartesianLayer
import com.patrykandpatrick.vico.compose.cartesian.rememberCartesianChart
import com.patrykandpatrick.vico.core.cartesian.data.CartesianChartModelProducer
import com.patrykandpatrick.vico.core.cartesian.data.lineSeries
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun VoltageChart(
    history: List<BatteryHistoryPoint>,
    modifier: Modifier = Modifier,
) {
    if (history.isEmpty()) {
        Text(
            text = "Pas d'historique disponible",
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.bodyMedium,
        )
        return
    }

    val modelProducer = remember { CartesianChartModelProducer() }

    // Update chart data when history changes
    remember(history) {
        modelProducer.runTransaction {
            lineSeries {
                series(
                    history.map { it.voltageMv / 1000.0 }
                )
            }
        }
    }

    CartesianChartHost(
        chart = rememberCartesianChart(
            rememberLineCartesianLayer(),
            startAxis = rememberStartAxis(
                title = "Tension (V)",
            ),
            bottomAxis = rememberBottomAxis(
                valueFormatter = { value, _, _ ->
                    val index = value.toInt().coerceIn(0, history.size - 1)
                    val ts = history.getOrNull(index)?.timestamp ?: 0L
                    val fmt = SimpleDateFormat("HH:mm", Locale.FRANCE)
                    fmt.format(Date(ts))
                },
            ),
        ),
        modelProducer = modelProducer,
        modifier = modifier,
    )
}
```

- [ ] **Step 3: Create ConfirmActionDialog**

```kotlin
package com.kxkm.bmu.ui.components

import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable

@Composable
fun ConfirmActionDialog(
    title: String,
    message: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(title) },
        text = { Text(message) },
        confirmButton = {
            TextButton(onClick = {
                onConfirm()
                onDismiss()
            }) {
                Text("Confirmer")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Annuler")
            }
        },
    )
}
```

- [ ] **Step 4: Create BatteryDetailScreen**

```kotlin
package com.kxkm.bmu.ui.detail

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.BoltOutlined
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.shared.model.BatteryState
import com.kxkm.bmu.ui.components.BatteryStateIcon
import com.kxkm.bmu.ui.components.ConfirmActionDialog
import com.kxkm.bmu.ui.theme.KxkmGreen
import com.kxkm.bmu.ui.theme.KxkmRed
import com.kxkm.bmu.util.ahDisplay
import com.kxkm.bmu.util.canControl
import com.kxkm.bmu.util.currentDisplay
import com.kxkm.bmu.util.displayName
import com.kxkm.bmu.util.voltageDisplay
import com.kxkm.bmu.viewmodel.AuthViewModel
import com.kxkm.bmu.viewmodel.BatteryDetailViewModel

@Composable
fun BatteryDetailScreen(
    batteryIndex: Int,
    authVM: AuthViewModel,
    viewModel: BatteryDetailViewModel = hiltViewModel(),
) {
    val battery by viewModel.battery.collectAsState()
    val history by viewModel.history.collectAsState()
    val commandResult by viewModel.commandResult.collectAsState()
    val currentUser by authVM.currentUser.collectAsState()

    var showConfirmDialog by remember { mutableStateOf(false) }
    var pendingSwitchOn by remember { mutableStateOf(true) }

    if (showConfirmDialog) {
        ConfirmActionDialog(
            title = if (pendingSwitchOn) "Connecter batterie ?" else "Déconnecter batterie ?",
            message = "Batterie ${batteryIndex + 1} — cette action est enregistrée dans l'audit.",
            onConfirm = { viewModel.switchBattery(pendingSwitchOn) },
            onDismiss = { showConfirmDialog = false },
        )
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(
            text = "Batterie ${batteryIndex + 1}",
            style = MaterialTheme.typography.headlineLarge,
        )

        // State card
        battery?.let { bat -> StateCard(bat) }

        // Chart
        Card(
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Historique tension (24h)",
                    style = MaterialTheme.typography.titleMedium,
                )
                Spacer(modifier = Modifier.height(8.dp))
                VoltageChart(
                    history = history,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(200.dp),
                )
            }
        }

        // Counters
        battery?.let { bat -> CountersCard(bat) }

        // Controls (role-gated)
        if (currentUser?.role?.canControl == true) {
            ControlsCard(
                onConnect = {
                    pendingSwitchOn = true
                    showConfirmDialog = true
                },
                onDisconnect = {
                    pendingSwitchOn = false
                    showConfirmDialog = true
                },
                onReset = { viewModel.resetSwitchCount() },
            )
        }

        // Command result
        commandResult?.let { result ->
            Text(
                text = result,
                style = MaterialTheme.typography.bodySmall,
                color = if (result == "OK" || result.startsWith("Compteur"))
                    MaterialTheme.colorScheme.primary
                else MaterialTheme.colorScheme.error,
            )
        }
    }
}

@Composable
private fun StateCard(battery: BatteryState) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column {
                Text(
                    text = battery.voltageMv.voltageDisplay(),
                    style = MaterialTheme.typography.headlineLarge,
                    fontFamily = FontFamily.Monospace,
                )
                Text(
                    text = battery.currentMa.currentDisplay(),
                    style = MaterialTheme.typography.titleMedium,
                    fontFamily = FontFamily.Monospace,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                BatteryStateIcon(state = battery.state)
                Text(
                    text = battery.state.displayName,
                    style = MaterialTheme.typography.labelSmall,
                )
            }
        }
    }
}

@Composable
private fun CountersCard(battery: BatteryState) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                text = "Compteurs",
                style = MaterialTheme.typography.titleMedium,
            )
            CounterRow("Décharge", battery.ahDischargeMah.ahDisplay())
            CounterRow("Charge", battery.ahChargeMah.ahDisplay())
            CounterRow("Nb switches", "${battery.nbSwitch}")
        }
    }
}

@Composable
private fun CounterRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(text = label, style = MaterialTheme.typography.bodyMedium)
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            fontFamily = FontFamily.Monospace,
        )
    }
}

@Composable
private fun ControlsCard(
    onConnect: () -> Unit,
    onDisconnect: () -> Unit,
    onReset: () -> Unit,
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                text = "Contrôle",
                style = MaterialTheme.typography.titleMedium,
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(
                    onClick = onConnect,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(containerColor = KxkmGreen),
                ) {
                    Icon(Icons.Filled.Bolt, contentDescription = null)
                    Text("Connecter", modifier = Modifier.padding(start = 4.dp))
                }
                Button(
                    onClick = onDisconnect,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(containerColor = KxkmRed),
                ) {
                    Icon(Icons.Filled.BoltOutlined, contentDescription = null)
                    Text("Déconnecter", modifier = Modifier.padding(start = 4.dp))
                }
            }
            OutlinedButton(
                onClick = onReset,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Icon(Icons.Filled.Refresh, contentDescription = null)
                Text("Reset compteur", modifier = Modifier.padding(start = 4.dp))
            }
        }
    }
}
```

- [ ] **Step 5: Build to verify**

```bash
./gradlew :androidApp:assembleDebug 2>&1 | tail -3
```

- [ ] **Step 6: Commit**

```bash
git add androidApp/src/main/java/com/kxkm/bmu/viewmodel/BatteryDetailViewModel.kt \
        androidApp/src/main/java/com/kxkm/bmu/ui/detail/ \
        androidApp/src/main/java/com/kxkm/bmu/ui/components/ConfirmActionDialog.kt
git commit -m "feat(android): battery detail with Vico chart, counters, and controls"
```

---

### Task 8: System screen — firmware + solar + connectivity

**Files:**
- Create: `androidApp/src/main/java/com/kxkm/bmu/viewmodel/SystemViewModel.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/system/SystemScreen.kt`

- [ ] **Step 1: Create SystemViewModel**

```kotlin
package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kxkm.bmu.shared.domain.MonitoringUseCase
import com.kxkm.bmu.shared.model.SolarData
import com.kxkm.bmu.shared.model.SystemInfo
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class SystemViewModel @Inject constructor(
    private val monitoringUseCase: MonitoringUseCase,
) : ViewModel() {

    private val _system = MutableStateFlow<SystemInfo?>(null)
    val system: StateFlow<SystemInfo?> = _system.asStateFlow()

    private val _solar = MutableStateFlow<SolarData?>(null)
    val solar: StateFlow<SolarData?> = _solar.asStateFlow()

    init {
        viewModelScope.launch {
            monitoringUseCase.observeSystem { info ->
                _system.value = info
            }
        }
        viewModelScope.launch {
            monitoringUseCase.observeSolar { data ->
                _solar.value = data
            }
        }
    }
}
```

- [ ] **Step 2: Create SystemScreen**

```kotlin
package com.kxkm.bmu.ui.system

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Cancel
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.util.formatBytes
import com.kxkm.bmu.util.formatUptime
import com.kxkm.bmu.viewmodel.SystemViewModel

@Composable
fun SystemScreen(
    viewModel: SystemViewModel = hiltViewModel(),
) {
    val system by viewModel.system.collectAsState()
    val solar by viewModel.solar.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(
            text = "Système",
            style = MaterialTheme.typography.headlineLarge,
        )

        val sys = system
        if (sys != null) {
            // Firmware card
            SectionCard("Firmware") {
                InfoRow("Version", sys.firmwareVersion)
                InfoRow("Uptime", sys.uptimeSeconds.formatUptime())
                InfoRow("Heap libre", sys.heapFree.formatBytes())
            }

            // Topology card
            SectionCard("Topologie") {
                InfoRow("INA237", "${sys.nbIna}")
                InfoRow("TCA9535", "${sys.nbTca}")
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text("Validation", style = MaterialTheme.typography.bodyMedium)
                    Icon(
                        imageVector = if (sys.topologyValid) Icons.Filled.CheckCircle
                        else Icons.Filled.Cancel,
                        contentDescription = null,
                        tint = if (sys.topologyValid) MaterialTheme.colorScheme.primary
                        else MaterialTheme.colorScheme.error,
                    )
                }
            }

            // WiFi card
            SectionCard("WiFi") {
                InfoRow("IP", sys.wifiIp ?: "Non connecté")
            }
        } else {
            CircularProgressIndicator(modifier = Modifier.align(Alignment.CenterHorizontally))
        }

        // Solar card
        solar?.let { sol ->
            SectionCard("Solaire (VE.Direct)") {
                InfoRow("Tension panneau", "%.1f V".format(sol.panelVoltageMv / 1000.0))
                InfoRow("Puissance", "${sol.panelPowerW} W")
                InfoRow("Tension batterie", "%.1f V".format(sol.batteryVoltageMv / 1000.0))
                InfoRow("Courant", "%.2f A".format(sol.batteryCurrentMa / 1000.0))
                InfoRow("État charge", chargeStateName(sol.chargeState))
                InfoRow("Production jour", "${sol.yieldTodayWh} Wh")
            }
        }
    }
}

@Composable
private fun SectionCard(
    title: String,
    content: @Composable () -> Unit,
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(text = title, style = MaterialTheme.typography.titleMedium)
            content()
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(text = label, style = MaterialTheme.typography.bodyMedium)
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            fontFamily = FontFamily.Monospace,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

private fun chargeStateName(cs: Int): String = when (cs) {
    0 -> "Off"
    2 -> "Fault"
    3 -> "Bulk"
    4 -> "Absorption"
    5 -> "Float"
    else -> "État $cs"
}
```

- [ ] **Step 3: Build to verify**

```bash
./gradlew :androidApp:assembleDebug 2>&1 | tail -3
```

- [ ] **Step 4: Commit**

```bash
git add androidApp/src/main/java/com/kxkm/bmu/viewmodel/SystemViewModel.kt \
        androidApp/src/main/java/com/kxkm/bmu/ui/system/
git commit -m "feat(android): system screen with firmware, topology, solar info"
```

---

### Task 9: Audit trail screen

**Files:**
- Create: `androidApp/src/main/java/com/kxkm/bmu/viewmodel/AuditViewModel.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/audit/AuditScreen.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/audit/AuditEventRow.kt`

- [ ] **Step 1: Create AuditViewModel**

```kotlin
package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import com.kxkm.bmu.shared.domain.AuditUseCase
import com.kxkm.bmu.shared.model.AuditEvent
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import javax.inject.Inject

@HiltViewModel
class AuditViewModel @Inject constructor(
    private val auditUseCase: AuditUseCase,
) : ViewModel() {

    private val _events = MutableStateFlow<List<AuditEvent>>(emptyList())
    val events: StateFlow<List<AuditEvent>> = _events.asStateFlow()

    private val _filterAction = MutableStateFlow<String?>(null)
    val filterAction: StateFlow<String?> = _filterAction.asStateFlow()

    private val _filterBattery = MutableStateFlow<Int?>(null)
    val filterBattery: StateFlow<Int?> = _filterBattery.asStateFlow()

    private val _pendingSyncCount = MutableStateFlow(0)
    val pendingSyncCount: StateFlow<Int> = _pendingSyncCount.asStateFlow()

    init {
        reload()
    }

    fun reload() {
        auditUseCase.getEvents(
            action = _filterAction.value,
            batteryIndex = _filterBattery.value,
        ) { result ->
            _events.value = result
        }
        _pendingSyncCount.value = auditUseCase.getPendingSyncCount()
    }

    fun setFilterAction(action: String?) {
        _filterAction.value = action
        reload()
    }

    fun clearFilters() {
        _filterAction.value = null
        _filterBattery.value = null
        reload()
    }
}
```

- [ ] **Step 2: Create AuditEventRow**

```kotlin
package com.kxkm.bmu.ui.audit

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.BoltOutlined
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import com.kxkm.bmu.shared.model.AuditEvent
import com.kxkm.bmu.ui.theme.KxkmBlue
import com.kxkm.bmu.ui.theme.KxkmGreen
import com.kxkm.bmu.ui.theme.KxkmRed
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun AuditEventRow(
    event: AuditEvent,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = 8.dp, horizontal = 16.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.Top,
    ) {
        Icon(
            imageVector = event.actionIcon,
            contentDescription = null,
            tint = event.actionColor,
            modifier = Modifier.size(24.dp),
        )

        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = event.action
                    .replace("_", " ")
                    .replaceFirstChar { it.uppercase() },
                style = MaterialTheme.typography.bodyMedium,
            )
            event.target?.let { target ->
                Text(
                    text = "Batterie ${target + 1}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            event.detail?.let { detail ->
                Text(
                    text = detail,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }

        Column(horizontalAlignment = Alignment.End) {
            Text(
                text = event.userId,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = formatTimestamp(event.timestamp),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

private val AuditEvent.actionIcon: ImageVector
    get() = when (action) {
        "switch_on" -> Icons.Filled.Bolt
        "switch_off" -> Icons.Filled.BoltOutlined
        "reset" -> Icons.Filled.Refresh
        "config_change" -> Icons.Filled.Settings
        "wifi_config" -> Icons.Filled.Wifi
        else -> Icons.Filled.Description
    }

private val AuditEvent.actionColor: Color
    get() = when (action) {
        "switch_on" -> KxkmGreen
        "switch_off" -> KxkmRed
        "config_change", "wifi_config" -> KxkmBlue
        else -> Color.Gray
    }

private fun formatTimestamp(ms: Long): String {
    val fmt = SimpleDateFormat("dd/MM HH:mm:ss", Locale.FRANCE)
    return fmt.format(Date(ms))
}
```

- [ ] **Step 3: Create AuditScreen**

```kotlin
package com.kxkm.bmu.ui.audit

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CloudUpload
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.viewmodel.AuditViewModel

@Composable
fun AuditScreen(
    viewModel: AuditViewModel = hiltViewModel(),
) {
    val events by viewModel.events.collectAsState()
    val filterAction by viewModel.filterAction.collectAsState()
    val pendingSync by viewModel.pendingSyncCount.collectAsState()

    LaunchedEffect(Unit) { viewModel.reload() }

    Column(modifier = Modifier.fillMaxSize()) {
        Text(
            text = "Audit",
            style = MaterialTheme.typography.headlineLarge,
            modifier = Modifier.padding(16.dp),
        )

        // Sync indicator
        if (pendingSync > 0) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Icon(
                    imageVector = Icons.Filled.CloudUpload,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.tertiary,
                )
                Text(
                    text = "$pendingSync événements en attente de sync",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.tertiary,
                )
            }
        }

        // Filter chips
        LazyRow(
            contentPadding = PaddingValues(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            item {
                FilterChip(
                    selected = filterAction == null,
                    onClick = { viewModel.clearFilters() },
                    label = { Text("Tous") },
                )
            }
            item {
                FilterChip(
                    selected = filterAction == "switch",
                    onClick = { viewModel.setFilterAction("switch") },
                    label = { Text("Switch") },
                )
            }
            item {
                FilterChip(
                    selected = filterAction == "config_change",
                    onClick = { viewModel.setFilterAction("config_change") },
                    label = { Text("Config") },
                )
            }
            item {
                FilterChip(
                    selected = filterAction == "reset",
                    onClick = { viewModel.setFilterAction("reset") },
                    label = { Text("Reset") },
                )
            }
        }

        HorizontalDivider(modifier = Modifier.padding(top = 8.dp))

        // Event list
        LazyColumn(modifier = Modifier.fillMaxSize()) {
            items(events, key = { it.timestamp }) { event ->
                AuditEventRow(event = event)
                HorizontalDivider()
            }
        }
    }
}
```

- [ ] **Step 4: Build to verify**

```bash
./gradlew :androidApp:assembleDebug 2>&1 | tail -3
```

- [ ] **Step 5: Commit**

```bash
git add androidApp/src/main/java/com/kxkm/bmu/viewmodel/AuditViewModel.kt \
        androidApp/src/main/java/com/kxkm/bmu/ui/audit/
git commit -m "feat(android): audit trail with filter chips and sync indicator"
```

---

### Task 10: Config screen — protection + WiFi + users + sync + transport

**Files:**
- Create: `androidApp/src/main/java/com/kxkm/bmu/viewmodel/ConfigViewModel.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/config/ConfigScreen.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/config/ProtectionConfigScreen.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/config/WifiConfigScreen.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/config/UserManagementScreen.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/config/SyncConfigScreen.kt`
- Create: `androidApp/src/main/java/com/kxkm/bmu/ui/config/TransportConfigScreen.kt`

- [ ] **Step 1: Create ConfigViewModel**

```kotlin
package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import com.kxkm.bmu.shared.auth.AuthUseCase
import com.kxkm.bmu.shared.domain.ConfigUseCase
import com.kxkm.bmu.shared.model.TransportChannel
import com.kxkm.bmu.shared.model.UserProfile
import com.kxkm.bmu.shared.model.UserRole
import com.kxkm.bmu.shared.model.WifiStatusInfo
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.Date
import javax.inject.Inject

@HiltViewModel
class ConfigViewModel @Inject constructor(
    private val configUseCase: ConfigUseCase,
    private val authUseCase: AuthUseCase,
) : ViewModel() {

    // Protection
    val minMv = MutableStateFlow(24000)
    val maxMv = MutableStateFlow(30000)
    val maxMa = MutableStateFlow(10000)
    val diffMv = MutableStateFlow(1000)

    // WiFi
    val wifiSsid = MutableStateFlow("")
    val wifiPassword = MutableStateFlow("")
    private val _wifiStatus = MutableStateFlow<WifiStatusInfo?>(null)
    val wifiStatus: StateFlow<WifiStatusInfo?> = _wifiStatus.asStateFlow()

    // Users
    private val _users = MutableStateFlow<List<UserProfile>>(emptyList())
    val users: StateFlow<List<UserProfile>> = _users.asStateFlow()

    // Sync
    val syncUrl = MutableStateFlow("")
    val mqttBroker = MutableStateFlow("")
    private val _syncPending = MutableStateFlow(0)
    val syncPending: StateFlow<Int> = _syncPending.asStateFlow()
    private val _lastSyncTime = MutableStateFlow<Date?>(null)
    val lastSyncTime: StateFlow<Date?> = _lastSyncTime.asStateFlow()

    // Transport
    val activeChannel = MutableStateFlow(TransportChannel.OFFLINE)
    val forceChannel = MutableStateFlow<TransportChannel?>(null)

    private val _statusMessage = MutableStateFlow<String?>(null)
    val statusMessage: StateFlow<String?> = _statusMessage.asStateFlow()

    init {
        loadAll()
    }

    fun loadAll() {
        val cfg = configUseCase.getCurrentConfig()
        minMv.value = cfg.minMv
        maxMv.value = cfg.maxMv
        maxMa.value = cfg.maxMa
        diffMv.value = cfg.diffMv

        _users.value = authUseCase.getAllUsers()
        _syncPending.value = configUseCase.getPendingSyncCount()
    }

    fun saveProtection() {
        configUseCase.setProtectionConfig(
            minMv = minMv.value,
            maxMv = maxMv.value,
            maxMa = maxMa.value,
            diffMv = diffMv.value,
        ) { result ->
            _statusMessage.value = if (result.isSuccess) "Seuils mis à jour" else "Erreur"
        }
    }

    fun sendWifiConfig() {
        configUseCase.setWifiConfig(
            ssid = wifiSsid.value,
            password = wifiPassword.value,
        ) { result ->
            _statusMessage.value = if (result.isSuccess) "WiFi configuré" else "Erreur (BLE requis)"
        }
    }

    fun deleteUser(user: UserProfile) {
        authUseCase.deleteUser(user.id)
        _users.value = authUseCase.getAllUsers()
    }

    fun createUser(name: String, pin: String, role: UserRole) {
        authUseCase.createUser(name, pin, role)
        _users.value = authUseCase.getAllUsers()
    }
}
```

- [ ] **Step 2: Create ConfigScreen (root)**

```kotlin
package com.kxkm.bmu.ui.config

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun ConfigScreen(
    onNavigateProtection: () -> Unit,
    onNavigateWifi: () -> Unit,
    onNavigateUsers: () -> Unit,
    onNavigateSync: () -> Unit,
    onNavigateTransport: () -> Unit,
) {
    Column(modifier = Modifier.fillMaxSize()) {
        Text(
            text = "Configuration",
            style = MaterialTheme.typography.headlineLarge,
            modifier = Modifier.padding(16.dp),
        )

        ConfigMenuItem("Protection", onNavigateProtection)
        HorizontalDivider()
        ConfigMenuItem("WiFi BMU", onNavigateWifi)
        HorizontalDivider()
        ConfigMenuItem("Utilisateurs", onNavigateUsers)
        HorizontalDivider()
        ConfigMenuItem("Sync cloud", onNavigateSync)
        HorizontalDivider()
        ConfigMenuItem("Transport", onNavigateTransport)
    }
}

@Composable
private fun ConfigMenuItem(title: String, onClick: () -> Unit) {
    ListItem(
        headlineContent = { Text(title) },
        trailingContent = {
            Icon(
                imageVector = Icons.AutoMirrored.Filled.ArrowForward,
                contentDescription = null,
            )
        },
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
    )
}
```

- [ ] **Step 3: Create ProtectionConfigScreen**

```kotlin
package com.kxkm.bmu.ui.config

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.viewmodel.ConfigViewModel

@Composable
fun ProtectionConfigScreen(
    viewModel: ConfigViewModel = hiltViewModel(),
) {
    val minMv by viewModel.minMv.collectAsState()
    val maxMv by viewModel.maxMv.collectAsState()
    val maxMa by viewModel.maxMa.collectAsState()
    val diffMv by viewModel.diffMv.collectAsState()
    val statusMessage by viewModel.statusMessage.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Protection", style = MaterialTheme.typography.headlineLarge)

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("Seuils de tension", style = MaterialTheme.typography.titleMedium)
                StepperRow("V min", minMv, "mV", step = 500, range = 20000..30000) {
                    viewModel.minMv.value = it
                }
                StepperRow("V max", maxMv, "mV", step = 500, range = 25000..35000) {
                    viewModel.maxMv.value = it
                }
                StepperRow("V diff max", diffMv, "mV", step = 100, range = 100..5000) {
                    viewModel.diffMv.value = it
                }
            }
        }

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("Seuil de courant", style = MaterialTheme.typography.titleMedium)
                StepperRow("I max", maxMa, "mA", step = 1000, range = 1000..50000) {
                    viewModel.maxMa.value = it
                }
            }
        }

        Button(
            onClick = { viewModel.saveProtection() },
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("Envoyer au BMU")
        }

        statusMessage?.let {
            Text(
                text = it,
                color = MaterialTheme.colorScheme.primary,
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }
}

@Composable
private fun StepperRow(
    label: String,
    value: Int,
    unit: String,
    step: Int,
    range: IntRange,
    onValueChange: (Int) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text = label, style = MaterialTheme.typography.bodyMedium)
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconButton(
                onClick = { if (value - step >= range.first) onValueChange(value - step) },
            ) {
                Icon(Icons.Filled.Remove, contentDescription = "Diminuer")
            }
            Text(
                text = "$value $unit",
                style = MaterialTheme.typography.bodyMedium,
                fontFamily = FontFamily.Monospace,
            )
            IconButton(
                onClick = { if (value + step <= range.last) onValueChange(value + step) },
            ) {
                Icon(Icons.Filled.Add, contentDescription = "Augmenter")
            }
        }
    }
}
```

- [ ] **Step 4: Create WifiConfigScreen**

```kotlin
package com.kxkm.bmu.ui.config

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.viewmodel.ConfigViewModel

@Composable
fun WifiConfigScreen(
    viewModel: ConfigViewModel = hiltViewModel(),
) {
    val ssid by viewModel.wifiSsid.collectAsState()
    val password by viewModel.wifiPassword.collectAsState()
    val wifiStatus by viewModel.wifiStatus.collectAsState()
    val statusMessage by viewModel.statusMessage.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("WiFi BMU", style = MaterialTheme.typography.headlineLarge)

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("Configuration WiFi du BMU", style = MaterialTheme.typography.titleMedium)

                OutlinedTextField(
                    value = ssid,
                    onValueChange = { viewModel.wifiSsid.value = it },
                    label = { Text("SSID") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )

                OutlinedTextField(
                    value = password,
                    onValueChange = { viewModel.wifiPassword.value = it },
                    label = { Text("Mot de passe") },
                    singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }

        Button(
            onClick = { viewModel.sendWifiConfig() },
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("Envoyer via BLE")
        }

        Text(
            text = "La config WiFi est envoyée via BLE uniquement.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        wifiStatus?.let { status ->
            Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
                Column(
                    modifier = Modifier.padding(16.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Text("État actuel", style = MaterialTheme.typography.titleMedium)
                    WifiInfoRow("SSID", status.ssid)
                    WifiInfoRow("IP", status.ip)
                    WifiInfoRow("RSSI", "${status.rssi} dBm")
                }
            }
        }

        statusMessage?.let {
            Text(text = it, color = MaterialTheme.colorScheme.primary)
        }
    }
}

@Composable
private fun WifiInfoRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(text = label, style = MaterialTheme.typography.bodyMedium)
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}
```

- [ ] **Step 5: Create UserManagementScreen**

```kotlin
package com.kxkm.bmu.ui.config

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.FloatingActionButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.shared.model.UserRole
import com.kxkm.bmu.util.displayName
import com.kxkm.bmu.viewmodel.ConfigViewModel

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun UserManagementScreen(
    viewModel: ConfigViewModel = hiltViewModel(),
) {
    val users by viewModel.users.collectAsState()
    var showAddDialog by remember { mutableStateOf(false) }

    Scaffold(
        floatingActionButton = {
            FloatingActionButton(onClick = { showAddDialog = true }) {
                Icon(Icons.Filled.Add, contentDescription = "Ajouter un utilisateur")
            }
        },
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding),
        ) {
            Text(
                text = "Utilisateurs",
                style = MaterialTheme.typography.headlineLarge,
                modifier = Modifier.padding(16.dp),
            )

            LazyColumn {
                items(users, key = { it.id }) { user ->
                    ListItem(
                        headlineContent = {
                            Text(user.name, style = MaterialTheme.typography.bodyLarge)
                        },
                        supportingContent = {
                            Text(
                                user.role.displayName,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        },
                        trailingContent = {
                            if (user.role != UserRole.ADMIN) {
                                IconButton(onClick = { viewModel.deleteUser(user) }) {
                                    Icon(
                                        Icons.Filled.Delete,
                                        contentDescription = "Supprimer",
                                        tint = MaterialTheme.colorScheme.error,
                                    )
                                }
                            }
                        },
                    )
                    HorizontalDivider()
                }
            }
        }
    }

    if (showAddDialog) {
        AddUserDialog(
            onDismiss = { showAddDialog = false },
            onConfirm = { name, pin, role ->
                viewModel.createUser(name, pin, role)
                showAddDialog = false
            },
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AddUserDialog(
    onDismiss: () -> Unit,
    onConfirm: (String, String, UserRole) -> Unit,
) {
    var name by remember { mutableStateOf("") }
    var pin by remember { mutableStateOf("") }
    var role by remember { mutableStateOf(UserRole.TECHNICIAN) }
    var roleMenuExpanded by remember { mutableStateOf(false) }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Nouveau profil") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    label = { Text("Nom") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                OutlinedTextField(
                    value = pin,
                    onValueChange = { if (it.length <= 6 && it.all(Char::isDigit)) pin = it },
                    label = { Text("PIN (6 chiffres)") },
                    singleLine = true,
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.NumberPassword),
                    modifier = Modifier.fillMaxWidth(),
                )

                ExposedDropdownMenuBox(
                    expanded = roleMenuExpanded,
                    onExpandedChange = { roleMenuExpanded = it },
                ) {
                    OutlinedTextField(
                        value = role.displayName,
                        onValueChange = {},
                        readOnly = true,
                        label = { Text("Rôle") },
                        trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = roleMenuExpanded) },
                        modifier = Modifier
                            .menuAnchor(MenuAnchorType.PrimaryNotEditable)
                            .fillMaxWidth(),
                    )
                    ExposedDropdownMenu(
                        expanded = roleMenuExpanded,
                        onDismissRequest = { roleMenuExpanded = false },
                    ) {
                        DropdownMenuItem(
                            text = { Text("Technicien") },
                            onClick = {
                                role = UserRole.TECHNICIAN
                                roleMenuExpanded = false
                            },
                        )
                        DropdownMenuItem(
                            text = { Text("Lecteur") },
                            onClick = {
                                role = UserRole.VIEWER
                                roleMenuExpanded = false
                            },
                        )
                    }
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = { onConfirm(name, pin, role) },
                enabled = name.isNotBlank() && pin.length == 6,
            ) {
                Text("Créer")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) {
                Text("Annuler")
            }
        },
    )
}
```

- [ ] **Step 6: Create SyncConfigScreen**

```kotlin
package com.kxkm.bmu.ui.config

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.viewmodel.ConfigViewModel
import java.text.DateFormat
import java.util.Date

@Composable
fun SyncConfigScreen(
    viewModel: ConfigViewModel = hiltViewModel(),
) {
    val syncUrl by viewModel.syncUrl.collectAsState()
    val mqttBroker by viewModel.mqttBroker.collectAsState()
    val syncPending by viewModel.syncPending.collectAsState()
    val lastSyncTime by viewModel.lastSyncTime.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Sync cloud", style = MaterialTheme.typography.headlineLarge)

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("kxkm-ai", style = MaterialTheme.typography.titleMedium)

                OutlinedTextField(
                    value = syncUrl,
                    onValueChange = { viewModel.syncUrl.value = it },
                    label = { Text("URL API") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                    modifier = Modifier.fillMaxWidth(),
                )

                OutlinedTextField(
                    value = mqttBroker,
                    onValueChange = { viewModel.mqttBroker.value = it },
                    label = { Text("Broker MQTT") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text("État", style = MaterialTheme.typography.titleMedium)

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text("En attente de sync")
                    Text(
                        text = "$syncPending",
                        color = if (syncPending > 0) MaterialTheme.colorScheme.tertiary
                        else MaterialTheme.colorScheme.primary,
                    )
                }

                lastSyncTime?.let { time ->
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                    ) {
                        Text("Dernier sync")
                        Text(
                            text = DateFormat.getDateTimeInstance(
                                DateFormat.SHORT, DateFormat.SHORT,
                            ).format(time),
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }
    }
}
```

- [ ] **Step 7: Create TransportConfigScreen**

```kotlin
package com.kxkm.bmu.ui.config

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.shared.model.TransportChannel
import com.kxkm.bmu.util.displayName
import com.kxkm.bmu.util.icon
import com.kxkm.bmu.viewmodel.ConfigViewModel

@Composable
fun TransportConfigScreen(
    viewModel: ConfigViewModel = hiltViewModel(),
) {
    val activeChannel by viewModel.activeChannel.collectAsState()
    val forceChannel by viewModel.forceChannel.collectAsState()

    val channelOptions = listOf<TransportChannel?>(
        null,
        TransportChannel.BLE,
        TransportChannel.WIFI,
        TransportChannel.MQTT_CLOUD,
    )

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Transport", style = MaterialTheme.typography.headlineLarge)

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text("Canal actif", style = MaterialTheme.typography.titleMedium)
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Icon(
                        imageVector = activeChannel.icon,
                        contentDescription = null,
                    )
                    Text(
                        text = activeChannel.displayName,
                        style = MaterialTheme.typography.bodyLarge,
                    )
                }
            }
        }

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("Forcer un canal", style = MaterialTheme.typography.titleMedium)

                SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                    channelOptions.forEachIndexed { index, channel ->
                        SegmentedButton(
                            selected = forceChannel == channel,
                            onClick = { viewModel.forceChannel.value = channel },
                            shape = SegmentedButtonDefaults.itemShape(
                                index = index,
                                count = channelOptions.size,
                            ),
                        ) {
                            Text(
                                text = channel?.displayName ?: "Auto",
                                style = MaterialTheme.typography.labelSmall,
                            )
                        }
                    }
                }

                Text(
                    text = "Auto = BLE > WiFi > Cloud > Offline",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
```

- [ ] **Step 8: Build to verify**

```bash
./gradlew :androidApp:assembleDebug 2>&1 | tail -3
```

- [ ] **Step 9: Commit**

```bash
git add androidApp/src/main/java/com/kxkm/bmu/viewmodel/ConfigViewModel.kt \
        androidApp/src/main/java/com/kxkm/bmu/ui/config/
git commit -m "feat(android): config screen — protection, WiFi, users, sync, transport"
```

---

### Final: Integration verification

**Files:** None (verification only)

- [ ] **Step 1: Full build**

```bash
./gradlew :androidApp:assembleDebug 2>&1 | tail -5
```
Expected: `BUILD SUCCESSFUL`

- [ ] **Step 2: Run on emulator**

```bash
# Start emulator if not running
emulator -avd Pixel_8_API_35 -no-audio -no-window &
adb wait-for-device
./gradlew :androidApp:installDebug
adb shell am start -n com.kxkm.bmu/.MainActivity
```

Verify on emulator:
- Onboarding screen appears (first launch)
- After creating admin -> bottom nav with 4 tabs visible
- Dashboard shows "Aucune batterie détectée" (no BMU connected)
- System screen loads (empty data)
- Config screen navigable with all 5 sub-screens

- [ ] **Step 3: Commit final**

```bash
git add -A
git commit -m "feat(android): complete Android Compose app — 5 screens, auth, controls"
```

---

## Dependencies on Plan 2 (KMP Shared)

This plan references these shared module types and factories that Plan 2 must provide:

**Types:** `BatteryState`, `BatteryStatus`, `SystemInfo`, `SolarData`, `AuditEvent`, `UserProfile`, `UserRole`, `TransportChannel`, `WifiStatusInfo`, `BatteryHistoryPoint`

**Factory:** `SharedFactory.create*()` methods for:
- `MonitoringUseCase` -- observeBatteries, observeBattery, observeSystem, observeSolar, getHistory
- `ControlUseCase` -- switchBattery, resetSwitchCount
- `ConfigUseCase` -- getCurrentConfig, setProtectionConfig, setWifiConfig, getPendingSyncCount
- `AuditUseCase` -- getEvents, getPendingSyncCount
- `AuthUseCase` -- authenticate, createUser, deleteUser, getAllUsers, hasNoUsers
- `TransportManager` -- channel StateFlow, isConnected, deviceName, rssi

**KMP->Android bridge:** On Android, KMP Kotlin types are used directly (no bridging layer needed unlike iOS). Use cases returning `Flow` can be collected natively with `viewModelScope.launch { flow.collect { ... } }`. Callback-style APIs are also supported.
