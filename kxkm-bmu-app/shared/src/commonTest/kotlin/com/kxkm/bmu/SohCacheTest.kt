package com.kxkm.bmu

import com.kxkm.bmu.model.*
import kotlinx.serialization.json.Json
import kotlin.test.Test
import kotlin.test.assertEquals

class SohCacheTest {

    private val json = Json { ignoreUnknownKeys = true }

    @Test
    fun mlScoreRoundTrip() {
        val original = MlScore(
            battery = 5,
            sohScore = 0.78f,
            rulDays = 95,
            anomalyScore = 0.22f,
            rIntTrendMohmPerDay = 0.04f,
            timestamp = 1743678000
        )
        val serialized = json.encodeToString(MlScore.serializer(), original)
        val deserialized = json.decodeFromString(MlScore.serializer(), serialized)
        assertEquals(original.battery, deserialized.battery)
        assertEquals(original.sohScore, deserialized.sohScore, 0.001f)
        assertEquals(original.rulDays, deserialized.rulDays)
    }

    @Test
    fun diagnosticRoundTrip() {
        val original = Diagnostic(
            battery = 2,
            diagnostic = "Batterie en d\u00e9gradation acc\u00e9l\u00e9r\u00e9e. Remplacement conseill\u00e9.",
            severity = DiagnosticSeverity.warning,
            generatedAt = 1743678000
        )
        val serialized = json.encodeToString(Diagnostic.serializer(), original)
        val deserialized = json.decodeFromString(Diagnostic.serializer(), serialized)
        assertEquals(original.diagnostic, deserialized.diagnostic)
        assertEquals(DiagnosticSeverity.warning, deserialized.severity)
    }

    @Test
    fun fleetHealthRoundTrip() {
        val original = FleetHealth(
            fleetHealth = 0.91f,
            outlierIdx = 7,
            outlierScore = 0.45f,
            imbalanceSeverity = 0.10f,
            timestamp = 1743678000
        )
        val serialized = json.encodeToString(FleetHealth.serializer(), original)
        val deserialized = json.decodeFromString(FleetHealth.serializer(), serialized)
        assertEquals(original.outlierIdx, deserialized.outlierIdx)
        assertEquals(original.fleetHealth, deserialized.fleetHealth, 0.001f)
    }
}
