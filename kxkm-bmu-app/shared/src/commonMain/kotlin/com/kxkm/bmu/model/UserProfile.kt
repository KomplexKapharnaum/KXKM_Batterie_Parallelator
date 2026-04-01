package com.kxkm.bmu.model

import kotlinx.serialization.Serializable

@Serializable
enum class UserRole { ADMIN, TECHNICIAN, VIEWER }

@Serializable
data class UserProfile(
    val id: String,
    val name: String,
    val role: UserRole,
    val pinHash: String,
    val salt: String
)
