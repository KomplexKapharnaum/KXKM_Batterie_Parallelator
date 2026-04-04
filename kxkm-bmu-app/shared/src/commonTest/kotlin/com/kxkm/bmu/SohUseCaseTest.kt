package com.kxkm.bmu

import com.kxkm.bmu.model.*
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SohUseCaseTest {

    @Test
    fun anomalyDetectionThresholds() {
        val scores = listOf(
            MlScore(0, 0.92f, 180, 0.05f, 0.01f, 1000),  // healthy
            MlScore(1, 0.65f, 30, 0.80f, 0.10f, 1000),   // anomaly + low SOH
            MlScore(2, 0.75f, 90, 0.30f, 0.02f, 1000),   // normal
            MlScore(3, 0.50f, 15, 0.40f, 0.15f, 1000),   // low SOH only
        )

        val alerts = scores.filter { score ->
            score.anomalyScore > 0.7f || (score.sohScore * 100).toInt() < 70
        }

        assertEquals(2, alerts.size)
        assertEquals(1, alerts[0].battery)  // anomaly + low SOH
        assertEquals(3, alerts[1].battery)  // low SOH only
    }

    @Test
    fun fleetHealthParsing() {
        val fleet = FleetHealth(
            fleetHealth = 0.88f,
            outlierIdx = 3,
            outlierScore = 0.72f,
            imbalanceSeverity = 0.15f,
            timestamp = 1743678000
        )
        assertEquals(88, (fleet.fleetHealth * 100).toInt())
        assertTrue(fleet.outlierScore > 0.3f)
    }

    @Test
    fun diagnosticSeverityMapping() {
        val diag = Diagnostic(
            battery = 0,
            diagnostic = "Test",
            severity = DiagnosticSeverity.critical,
            generatedAt = 1000
        )
        assertEquals(DiagnosticSeverity.critical, diag.severity)
    }
}
