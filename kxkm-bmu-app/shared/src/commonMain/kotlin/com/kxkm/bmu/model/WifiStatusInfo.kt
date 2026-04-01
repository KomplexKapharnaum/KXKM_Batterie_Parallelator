package com.kxkm.bmu.model

import kotlinx.serialization.Serializable

@Serializable
data class WifiStatusInfo(
    val ssid: String,
    val ip: String,
    val rssi: Int,
    val connected: Boolean
)
