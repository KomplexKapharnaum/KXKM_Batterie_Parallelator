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
