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
