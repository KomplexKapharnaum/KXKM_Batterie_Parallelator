package com.kxkm.bmu.model

import kotlinx.serialization.Serializable

@Serializable
data class AuditEvent(
    val timestamp: Long,
    val userId: String,
    val action: String,
    val target: Int? = null,
    val detail: String? = null
)
