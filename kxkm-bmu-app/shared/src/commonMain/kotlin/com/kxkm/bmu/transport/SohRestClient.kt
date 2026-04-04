package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import io.ktor.client.*
import io.ktor.client.request.*
import io.ktor.client.statement.*
import kotlinx.serialization.json.Json

/**
 * REST client for kxkm-ai:8400 SOH scoring and diagnostic endpoints.
 * Mirrors CloudRestClient conventions (auth header, Json config).
 */
class SohRestClient(
    private val baseUrl: String = "http://kxkm-ai:8400",
    private val apiKey: String = ""
) {
    private val client = HttpClient()
    private val json = Json { ignoreUnknownKeys = true }

    /** GET /api/soh/batteries — all battery ML scores (latest) */
    suspend fun getAllScores(): List<MlScore> {
        val resp = client.get("$baseUrl/api/soh/batteries") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<List<MlScore>>(resp)
    }

    /** GET /api/soh/battery/{id} — single battery score */
    suspend fun getBatteryScore(batteryIndex: Int): MlScore {
        val resp = client.get("$baseUrl/api/soh/battery/$batteryIndex") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<MlScore>(resp)
    }

    /** GET /api/soh/fleet — fleet health (GNN output) */
    suspend fun getFleetHealth(): FleetHealth {
        val resp = client.get("$baseUrl/api/soh/fleet") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<FleetHealth>(resp)
    }

    /** GET /api/soh/history/{id}?days=7 — SOH/R_int time series */
    suspend fun getSohHistory(batteryIndex: Int, days: Int = 7): List<MlScore> {
        val resp = client.get("$baseUrl/api/soh/history/$batteryIndex") {
            header("Authorization", "Bearer $apiKey")
            parameter("days", days)
        }.bodyAsText()
        return json.decodeFromString<List<MlScore>>(resp)
    }

    /** GET /api/diagnostic/{battery_id} — cached daily diagnostic */
    suspend fun getDiagnostic(batteryIndex: Int): Diagnostic {
        val resp = client.get("$baseUrl/api/diagnostic/$batteryIndex") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<Diagnostic>(resp)
    }

    /** POST /api/diagnostic/{battery_id} — generate fresh diagnostic on-demand */
    suspend fun refreshDiagnostic(batteryIndex: Int): Diagnostic {
        val resp = client.post("$baseUrl/api/diagnostic/$batteryIndex") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<Diagnostic>(resp)
    }

    /** GET /api/diagnostic/fleet — fleet-level summary */
    suspend fun getFleetDiagnostic(): FleetDiagnostic {
        val resp = client.get("$baseUrl/api/diagnostic/fleet") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<FleetDiagnostic>(resp)
    }

    /** POST /api/soh/predict — force refresh ML inference */
    suspend fun forceRefresh(): Boolean {
        val resp = client.post("$baseUrl/api/soh/predict") {
            header("Authorization", "Bearer $apiKey")
        }
        return resp.status.value in 200..299
    }
}
