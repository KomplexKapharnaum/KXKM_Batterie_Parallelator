package com.kxkm.bmu.model

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
enum class DiagnosticSeverity { info, warning, critical }

@Serializable
data class Diagnostic(
    val battery: Int,
    val diagnostic: String,
    val severity: DiagnosticSeverity,
    @SerialName("generated_at") val generatedAt: Long
)

@Serializable
data class FleetDiagnostic(
    val summary: String,
    val severity: DiagnosticSeverity,
    @SerialName("generated_at") val generatedAt: Long
)
