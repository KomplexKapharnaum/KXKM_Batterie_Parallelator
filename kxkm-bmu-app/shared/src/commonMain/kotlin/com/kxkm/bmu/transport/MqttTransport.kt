package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.serialization.json.Json

/** MQTT transport — read-only, subscribes to bmu/battery/# on kxkm-ai broker */
class MqttTransport(
    private val brokerUrl: String,
    private val username: String,
    private val password: String
) : Transport {
    override val channel = TransportChannel.MQTT_CLOUD
    private val _isConnected = MutableStateFlow(false)
    override val isConnected: StateFlow<Boolean> = _isConnected
    override val capabilities = setOf(TransportCapability.OBSERVE)

    private val _batteries = MutableStateFlow<List<BatteryState>>(emptyList())
    private val _system = MutableStateFlow<SystemInfo?>(null)
    private val _solar = MutableStateFlow<SolarData?>(null)
    private val json = Json { ignoreUnknownKeys = true }

    override fun observeBatteries(): Flow<List<BatteryState>> = _batteries
    override fun observeSystem(): Flow<SystemInfo?> = _system
    override fun observeSolar(): Flow<SolarData?> = _solar

    override suspend fun connect() {
        // Platform-specific MQTT implementation
        // Android: Paho MQTT client
        // iOS: Custom wrapper or CocoaMQTT via expect/actual
        // Subscribes to bmu/battery/# and parses JSON payloads
        _isConnected.value = false
    }

    override suspend fun disconnect() {
        _isConnected.value = false
    }

    // Read-only transport — all commands throw
    override suspend fun switchBattery(index: Int, on: Boolean) =
        CommandResult.error("Control not available via cloud")
    override suspend fun resetSwitchCount(index: Int) =
        CommandResult.error("Control not available via cloud")
    override suspend fun setProtectionConfig(config: ProtectionConfig) =
        CommandResult.error("Control not available via cloud")
    override suspend fun setWifiConfig(ssid: String, password: String) =
        CommandResult.error("Control not available via cloud")

    /** Called by platform-specific MQTT callback when message received */
    fun onMqttMessage(topic: String, payload: String) {
        try {
            // Topic format: bmu/battery/N
            val parts = topic.split("/")
            if (parts.size >= 3 && parts[0] == "bmu" && parts[1] == "battery") {
                val batteryJson = json.decodeFromString<MqttBatteryPayload>(payload)
                val index = parts[2].toIntOrNull()?.minus(1) ?: return
                val state = BatteryState(
                    index = index,
                    voltageMv = batteryJson.v_mv.toInt(),
                    currentMa = (batteryJson.i_a * 1000).toInt(),
                    state = BatteryStatus.valueOf(batteryJson.state.uppercase()),
                    ahDischargeMah = (batteryJson.ah_d * 1000).toInt(),
                    ahChargeMah = (batteryJson.ah_c * 1000).toInt(),
                    nbSwitch = 0
                )
                val current = _batteries.value.toMutableList()
                val idx = current.indexOfFirst { it.index == index }
                if (idx >= 0) current[idx] = state else current.add(state)
                _batteries.value = current.sortedBy { it.index }
            }
        } catch (_: Exception) { }
    }
}

@kotlinx.serialization.Serializable
private data class MqttBatteryPayload(
    val bat: Int,
    val v_mv: Float,
    val i_a: Float,
    val ah_d: Float,
    val ah_c: Float,
    val state: String
)
