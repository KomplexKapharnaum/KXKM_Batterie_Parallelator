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
