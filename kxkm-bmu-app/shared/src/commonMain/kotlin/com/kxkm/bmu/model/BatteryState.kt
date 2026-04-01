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
