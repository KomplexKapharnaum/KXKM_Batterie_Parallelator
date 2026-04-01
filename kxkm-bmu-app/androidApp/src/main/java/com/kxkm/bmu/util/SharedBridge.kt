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
        BatteryStatus.CONNECTED -> "Connect\u00e9"
        BatteryStatus.DISCONNECTED -> "D\u00e9connect\u00e9"
        BatteryStatus.RECONNECTING -> "Reconnexion"
        BatteryStatus.ERROR -> "Erreur"
        BatteryStatus.LOCKED -> "Verrouill\u00e9"
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
