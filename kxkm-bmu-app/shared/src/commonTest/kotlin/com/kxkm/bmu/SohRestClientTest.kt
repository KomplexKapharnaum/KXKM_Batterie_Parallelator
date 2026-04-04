package com.kxkm.bmu

import com.kxkm.bmu.model.*
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class SohRestClientTest {

    private val json = Json { ignoreUnknownKeys = true }

    @Test
    fun parsesMlScoreResponse() {
        val input = """
            {"battery":3,"soh_score":0.87,"rul_days":142,"anomaly_score":0.12,
             "r_int_trend_mohm_per_day":0.08,"timestamp":1743678000}
        """.trimIndent()
        val score = json.decodeFromString<MlScore>(input)
        assertEquals(3, score.battery)
        assertEquals(0.87f, score.sohScore, 0.01f)
        assertEquals(142, score.rulDays)
        assertEquals(0.12f, score.anomalyScore, 0.01f)
        assertEquals(0.08f, score.rIntTrendMohmPerDay, 0.01f)
    }

    @Test
    fun parsesFleetHealthResponse() {
        val input = """
            {"fleet_health":0.88,"outlier_idx":3,"outlier_score":0.72,
             "imbalance_severity":0.15,"timestamp":1743678000}
        """.trimIndent()
        val fleet = json.decodeFromString<FleetHealth>(input)
        assertEquals(0.88f, fleet.fleetHealth, 0.01f)
        assertEquals(3, fleet.outlierIdx)
        assertEquals(0.72f, fleet.outlierScore, 0.01f)
    }

    @Test
    fun parsesDiagnosticResponse() {
        val input = """
            {"battery":3,
             "diagnostic":"Batterie 3 en bon \u00e9tat (SOH 87%). Aucune action requise.",
             "severity":"info",
             "generated_at":1743678000}
        """.trimIndent()
        val diag = json.decodeFromString<Diagnostic>(input)
        assertEquals(3, diag.battery)
        assertEquals(DiagnosticSeverity.info, diag.severity)
        assertTrue(diag.diagnostic.contains("Batterie 3"))
    }

    @Test
    fun parsesScoreList() {
        val scores = listOf(
            MlScore(0, 0.92f, 180, 0.05f, 0.01f, 1743678000),
            MlScore(1, 0.85f, 120, 0.15f, 0.03f, 1743678000),
        )
        val serialized = json.encodeToString(scores)
        val parsed = json.decodeFromString<List<MlScore>>(serialized)
        assertEquals(2, parsed.size)
        assertEquals(0.92f, parsed[0].sohScore, 0.01f)
    }
}
