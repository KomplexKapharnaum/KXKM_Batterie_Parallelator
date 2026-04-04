package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import io.ktor.client.*
import io.ktor.client.plugins.contentnegotiation.*
import io.ktor.client.plugins.websocket.*
import io.ktor.client.request.contentType
import io.ktor.client.request.*
import io.ktor.client.statement.*
import io.ktor.http.ContentType
import io.ktor.serialization.kotlinx.json.*
import io.ktor.websocket.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

class WifiTransport(private val baseUrl: String, private val token: String) : Transport {
    override val channel = TransportChannel.WIFI
    private val _isConnected = MutableStateFlow(false)
    override val isConnected: StateFlow<Boolean> = _isConnected
    override val capabilities = setOf(
        TransportCapability.OBSERVE,
        TransportCapability.SWITCH_BATTERY
    )

    private val _batteries = MutableStateFlow<List<BatteryState>>(emptyList())
    private val _system = MutableStateFlow<SystemInfo?>(null)
    private val _solar = MutableStateFlow<SolarData?>(null)
    private var scope: CoroutineScope = newScope()
    private val json = Json { ignoreUnknownKeys = true }

    private fun newScope(): CoroutineScope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    private fun ensureActiveScope() {
        if (!scope.isActive) {
            scope = newScope()
        }
    }

    private val client = HttpClient {
        install(ContentNegotiation) { json(json) }
        install(WebSockets)
    }

    override fun observeBatteries(): Flow<List<BatteryState>> = _batteries
    override fun observeSystem(): Flow<SystemInfo?> = _system
    override fun observeSolar(): Flow<SolarData?> = _solar

    override suspend fun connect() {
        ensureActiveScope()
        // Start WebSocket connection for real-time battery updates
        scope.launch {
            try {
                client.webSocket("$baseUrl/ws") {
                    send(json.encodeToString(WsAuthFrame(auth = token)))
                    val authResp = (incoming.receive() as? Frame.Text)?.readText() ?: ""
                    if ("ok" !in authResp) {
                        _isConnected.value = false
                        return@webSocket
                    }
                    _isConnected.value = true

                    for (frame in incoming) {
                        if (frame is Frame.Text) {
                            try {
                                val data = json.decodeFromString<WsBatteryPush>(frame.readText())
                                _batteries.value = data.batteries
                            } catch (_: Exception) { }
                        }
                    }
                }
            } catch (_: Exception) {
                _isConnected.value = false
            }
        }

        // Periodic system + solar polling (every 5s)
        scope.launch {
            while (true) {
                try {
                    val sysResp = client.get("$baseUrl/api/system").bodyAsText()
                    _system.value = json.decodeFromString<SystemInfo>(sysResp)

                    val solarResp = client.get("$baseUrl/api/solar").bodyAsText()
                    _solar.value = json.decodeFromString<SolarData>(solarResp)
                } catch (_: Exception) { }
                delay(5000)
            }
        }
    }

    override suspend fun disconnect() {
        scope.cancel()
        _isConnected.value = false
    }

    override fun close() {
        scope.cancel()
        client.close()
        _isConnected.value = false
    }

    override suspend fun switchBattery(index: Int, on: Boolean): CommandResult {
        val payload = json.encodeToString(SwitchCommand(battery = index))
        return postMutation(
            if (on) "/api/battery/switch_on" else "/api/battery/switch_off",
            payload
        )
    }

    override suspend fun resetSwitchCount(index: Int): CommandResult {
        return CommandResult.error("Reset not available via WiFi API")
    }

    override suspend fun setProtectionConfig(config: ProtectionConfig): CommandResult {
        return CommandResult.error("Config not available via WiFi API")
    }

    override suspend fun setWifiConfig(ssid: String, password: String): CommandResult {
        return CommandResult.error("WiFi config requires BLE")
    }

    private suspend fun postMutation(path: String, body: String): CommandResult {
        return try {
            val resp = client.post("$baseUrl$path") {
                header("Authorization", "Bearer $token")
                contentType(ContentType.Application.Json)
                setBody(body)
            }
            if (resp.status.value in 200..299) CommandResult.ok()
            else CommandResult.error("HTTP ${resp.status.value}")
        } catch (e: Exception) {
            CommandResult.error(e.message ?: "Network error")
        }
    }
}

@Serializable
private data class WsBatteryPush(val batteries: List<BatteryState>)

@Serializable
private data class WsAuthFrame(val auth: String)

@Serializable
private data class SwitchCommand(val battery: Int)
