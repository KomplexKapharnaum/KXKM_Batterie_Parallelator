package com.kxkm.bmu

import com.kxkm.bmu.model.BatteryState
import com.kxkm.bmu.model.CommandResult
import com.kxkm.bmu.model.ProtectionConfig
import com.kxkm.bmu.model.SolarData
import com.kxkm.bmu.model.SystemInfo
import com.kxkm.bmu.model.TransportChannel
import com.kxkm.bmu.transport.Transport
import com.kxkm.bmu.transport.TransportCapability
import com.kxkm.bmu.transport.TransportManager
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.flowOf
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

private class FakeTransport(
    override val channel: TransportChannel,
    connected: Boolean,
    override val capabilities: Set<TransportCapability>
) : Transport {
    private val _isConnected = MutableStateFlow(connected)
    override val isConnected: StateFlow<Boolean> = _isConnected

    override fun observeBatteries(): Flow<List<BatteryState>> = flowOf(emptyList())
    override fun observeSystem(): Flow<SystemInfo?> = flowOf(null)
    override fun observeSolar(): Flow<SolarData?> = flowOf(null)

    override suspend fun switchBattery(index: Int, on: Boolean): CommandResult = CommandResult.ok()
    override suspend fun resetSwitchCount(index: Int): CommandResult = CommandResult.ok()
    override suspend fun setProtectionConfig(config: ProtectionConfig): CommandResult = CommandResult.ok()
    override suspend fun setWifiConfig(ssid: String, password: String): CommandResult = CommandResult.ok()
    override suspend fun connect() { _isConnected.value = true }
    override suspend fun disconnect() { _isConnected.value = false }
}

class TransportManagerTest {
    @Test
    fun fallbackOrder() {
        val priority = TransportManager.PRIORITY_ORDER
        assertEquals(TransportChannel.BLE, priority[0])
        assertEquals(TransportChannel.WIFI, priority[1])
        assertEquals(TransportChannel.MQTT_CLOUD, priority[2])
        assertEquals(TransportChannel.OFFLINE, priority[3])
    }

    @Test
    fun selectWifiWhenBleDisconnected() {
        val ble = FakeTransport(TransportChannel.BLE, connected = false,
            capabilities = setOf(TransportCapability.OBSERVE))
        val wifi = FakeTransport(TransportChannel.WIFI, connected = true,
            capabilities = setOf(TransportCapability.OBSERVE, TransportCapability.SWITCH_BATTERY))
        val mqtt = FakeTransport(TransportChannel.MQTT_CLOUD, connected = false,
            capabilities = setOf(TransportCapability.OBSERVE))
        val offline = FakeTransport(TransportChannel.OFFLINE, connected = false,
            capabilities = setOf(TransportCapability.OBSERVE))

        val manager = TransportManager(ble, wifi, mqtt, offline)
        manager.setWifi(wifi)

        assertEquals(TransportChannel.WIFI, manager.activeChannel.value)
        assertTrue(manager.supports(TransportCapability.SWITCH_BATTERY))
        assertFalse(manager.supports(TransportCapability.SET_CONFIG))
    }

    @Test
    fun fallbackToOfflineWhenNoTransportConnected() {
        val ble = FakeTransport(TransportChannel.BLE, connected = false,
            capabilities = setOf(TransportCapability.OBSERVE))
        val wifi = FakeTransport(TransportChannel.WIFI, connected = false,
            capabilities = setOf(TransportCapability.OBSERVE, TransportCapability.SWITCH_BATTERY))
        val mqtt = FakeTransport(TransportChannel.MQTT_CLOUD, connected = false,
            capabilities = setOf(TransportCapability.OBSERVE))
        val offline = FakeTransport(TransportChannel.OFFLINE, connected = false,
            capabilities = setOf(TransportCapability.OBSERVE))

        val manager = TransportManager(ble, wifi, mqtt, offline)
        manager.setWifi(wifi)
        manager.setMqtt(mqtt)

        assertEquals(TransportChannel.OFFLINE, manager.activeChannel.value)
        assertTrue(manager.supports(TransportCapability.OBSERVE))
        assertFalse(manager.supports(TransportCapability.SWITCH_BATTERY))
    }
}
