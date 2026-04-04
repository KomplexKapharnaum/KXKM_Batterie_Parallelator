package com.kxkm.bmu

import com.kxkm.bmu.domain.ConfigUseCase
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
import kotlinx.coroutines.test.runTest
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

private class ConfigFakeTransport(
    override val channel: TransportChannel,
    connected: Boolean,
    override val capabilities: Set<TransportCapability>,
    private val configResult: CommandResult = CommandResult.ok(),
    private val wifiResult: CommandResult = CommandResult.ok()
) : Transport {
    private val _isConnected = MutableStateFlow(connected)
    override val isConnected: StateFlow<Boolean> = _isConnected

    var configCalls = 0
    var wifiCalls = 0

    override fun observeBatteries(): Flow<List<BatteryState>> = flowOf(emptyList())
    override fun observeSystem(): Flow<SystemInfo?> = flowOf(null)
    override fun observeSolar(): Flow<SolarData?> = flowOf(null)

    override suspend fun switchBattery(index: Int, on: Boolean): CommandResult =
        CommandResult.error("Not used in this test")

    override suspend fun resetSwitchCount(index: Int): CommandResult =
        CommandResult.error("Not used in this test")

    override suspend fun setProtectionConfig(config: ProtectionConfig): CommandResult {
        configCalls++
        return configResult
    }

    override suspend fun setWifiConfig(ssid: String, password: String): CommandResult {
        wifiCalls++
        return wifiResult
    }

    override suspend fun connect() { _isConnected.value = true }
    override suspend fun disconnect() { _isConnected.value = false }
}

class ConfigUseCaseTest {
    @Test
    fun setProtectionConfigRequiresCapability() = runTest {
        val ble = ConfigFakeTransport(
            channel = TransportChannel.BLE,
            connected = true,
            capabilities = setOf(TransportCapability.OBSERVE)
        )
        val manager = TransportManager(ble, null, null, ble)
        val useCase = ConfigUseCase(manager) { _, _, _ -> }

        val result = useCase.setProtectionConfig(24000, 28800, 10000, 300)

        assertFalse(result.isSuccess)
        assertEquals(0, ble.configCalls)
    }

    @Test
    fun setProtectionConfigSuccessRecordsAudit() = runTest {
        val ble = ConfigFakeTransport(
            channel = TransportChannel.BLE,
            connected = true,
            capabilities = setOf(TransportCapability.OBSERVE, TransportCapability.SET_CONFIG)
        )
        val manager = TransportManager(ble, null, null, ble)
        var auditAction = ""
        val useCase = ConfigUseCase(manager) { action, _, _ -> auditAction = action }

        val result = useCase.setProtectionConfig(24000, 28800, 10000, 300)

        assertTrue(result.isSuccess)
        assertEquals(1, ble.configCalls)
        assertEquals("config_change", auditAction)
    }

    @Test
    fun setWifiConfigRequiresCapability() = runTest {
        val ble = ConfigFakeTransport(
            channel = TransportChannel.BLE,
            connected = true,
            capabilities = setOf(TransportCapability.OBSERVE, TransportCapability.SET_CONFIG)
        )
        val manager = TransportManager(ble, null, null, ble)
        val useCase = ConfigUseCase(manager) { _, _, _ -> }

        val result = useCase.setWifiConfig("ssid", "pwd")

        assertFalse(result.isSuccess)
        assertEquals(0, ble.wifiCalls)
    }
}
