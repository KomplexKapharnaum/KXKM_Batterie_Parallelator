package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import com.kxkm.bmu.db.BatteryHistoryPoint
import io.ktor.client.*
import io.ktor.client.request.*
import io.ktor.client.statement.*
import kotlinx.serialization.json.Json

/** REST client for kxkm-ai API — history queries and sync push */
class CloudRestClient(
    private val baseUrl: String,
    private val apiKey: String
) {
    private val client = HttpClient()
    private val json = Json { ignoreUnknownKeys = true }

    suspend fun getBatteries(): List<BatteryState> {
        val resp = client.get("$baseUrl/api/bmu/batteries") {
            header("Authorization", "Bearer $apiKey")
        }.bodyAsText()
        return json.decodeFromString<List<BatteryState>>(resp)
    }

    suspend fun getHistory(batteryIndex: Int, fromMs: Long, toMs: Long): List<BatteryHistoryPoint> {
        val resp = client.get("$baseUrl/api/bmu/history") {
            header("Authorization", "Bearer $apiKey")
            parameter("battery", batteryIndex)
            parameter("from", fromMs)
            parameter("to", toMs)
        }.bodyAsText()
        return json.decodeFromString<List<BatteryHistoryPoint>>(resp)
    }

    suspend fun getAuditEvents(
        fromMs: Long? = null, toMs: Long? = null,
        user: String? = null, action: String? = null
    ): List<AuditEvent> {
        val resp = client.get("$baseUrl/api/bmu/audit") {
            header("Authorization", "Bearer $apiKey")
            fromMs?.let { parameter("from", it) }
            toMs?.let { parameter("to", it) }
            user?.let { parameter("user", it) }
            action?.let { parameter("action", it) }
        }.bodyAsText()
        return json.decodeFromString<List<AuditEvent>>(resp)
    }

    suspend fun syncBatch(payload: String): Boolean {
        val resp = client.post("$baseUrl/api/bmu/sync") {
            header("Authorization", "Bearer $apiKey")
            header("Content-Type", "application/json")
            setBody(payload)
        }
        return resp.status.value in 200..299
    }
}
