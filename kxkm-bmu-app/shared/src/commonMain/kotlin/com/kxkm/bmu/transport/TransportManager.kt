package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

class TransportManager(
    private val ble: Transport,
    private var wifi: Transport?,
    private var mqtt: Transport?,
    private val offline: Transport
) {
    companion object {
        val PRIORITY_ORDER = listOf(
            TransportChannel.BLE,
            TransportChannel.WIFI,
            TransportChannel.MQTT_CLOUD,
            TransportChannel.OFFLINE
        )
    }

    private val _activeChannel = MutableStateFlow(TransportChannel.OFFLINE)
    val activeChannel: StateFlow<TransportChannel> = _activeChannel

    private val _activeTransport = MutableStateFlow<Transport>(offline)
    val activeTransport: StateFlow<Transport> = _activeTransport

    /** Force a specific channel (null = auto) */
    var forcedChannel: TransportChannel? = null

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    fun start() {
        scope.launch {
            while (true) {
                updateActiveTransport()
                delay(2000) // Re-evaluate every 2s
            }
        }
    }

    fun setWifi(transport: WifiTransport?) {
        wifi = transport
        updateActiveTransport()
    }

    fun setMqtt(transport: MqttTransport?) {
        mqtt = transport
        updateActiveTransport()
    }

    private fun updateActiveTransport() {
        val best = selectBestTransport()
        if (best.channel != _activeChannel.value) {
            _activeTransport.value = best
            _activeChannel.value = best.channel
        }
    }

    private fun selectBestTransport(): Transport {
        forcedChannel?.let { forced ->
            return getTransport(forced)
        }

        // Auto: try in priority order
        if (ble.isConnected.value) return ble
        if (wifi?.isConnected?.value == true) return wifi
        if (mqtt?.isConnected?.value == true) return mqtt ?: offline
        return offline
    }

    private fun getTransport(channel: TransportChannel): Transport {
        return when (channel) {
            TransportChannel.BLE -> ble
            TransportChannel.WIFI -> wifi ?: offline
            TransportChannel.MQTT_CLOUD -> mqtt ?: offline
            TransportChannel.REST_CLOUD -> offline // REST is query-only, not a live transport
            TransportChannel.OFFLINE -> offline
        }
    }

    /** Reactive battery stream from active transport */
    fun observeBatteries(): Flow<List<BatteryState>> =
        _activeTransport.flatMapLatest { it.observeBatteries() }

    fun observeSystem(): Flow<SystemInfo?> =
        _activeTransport.flatMapLatest { it.observeSystem() }

    fun observeSolar(): Flow<SolarData?> =
        _activeTransport.flatMapLatest { it.observeSolar() }

    fun observeHealth(): Flow<List<BatteryHealth>> =
        _activeTransport.flatMapLatest { it.observeHealth() }

    suspend fun triggerRintMeasurement(batteryIndex: Int): CommandResult =
        _activeTransport.value.triggerRintMeasurement(batteryIndex)

    suspend fun switchBattery(index: Int, on: Boolean): CommandResult =
        _activeTransport.value.switchBattery(index, on)

    suspend fun resetSwitchCount(index: Int): CommandResult =
        _activeTransport.value.resetSwitchCount(index)

    suspend fun setProtectionConfig(config: ProtectionConfig): CommandResult =
        _activeTransport.value.setProtectionConfig(config)

    suspend fun setWifiConfig(ssid: String, password: String): CommandResult =
        _activeTransport.value.setWifiConfig(ssid, password)

    fun supports(capability: TransportCapability): Boolean =
        _activeTransport.value.supports(capability)

    fun close() {
        scope.cancel()
        ble.close()
        wifi?.close()
        mqtt?.close()
        offline.close()
    }
}
