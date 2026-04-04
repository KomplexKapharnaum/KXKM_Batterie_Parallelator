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

@Serializable
data class BatteryHealth(
    val index: Int,
    val sohPercent: Int,           // 0-100, 0 = not computed
    val rOhmicMohm: Float,        // Ohmic resistance in mOhm
    val rTotalMohm: Float,        // Total resistance in mOhm
    val rintValid: Boolean,       // R_int measurement valid
    val sohConfidence: Int,       // 0-100
    val timestamp: Long = 0L      // Epoch ms (set by app on reception)
)
