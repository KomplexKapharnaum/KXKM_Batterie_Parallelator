package com.kxkm.bmu.model

import kotlinx.serialization.Serializable

@Serializable
data class ProtectionConfig(
    val minMv: Int = 24000,
    val maxMv: Int = 30000,
    val maxMa: Int = 10000,
    val diffMv: Int = 1000
)
