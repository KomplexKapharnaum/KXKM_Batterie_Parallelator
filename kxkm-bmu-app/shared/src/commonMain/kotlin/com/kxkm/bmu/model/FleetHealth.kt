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
