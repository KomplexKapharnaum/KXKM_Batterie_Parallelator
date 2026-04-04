package com.kxkm.bmu

import com.kxkm.bmu.domain.ControlUseCase
import com.kxkm.bmu.model.BatteryState
import com.kxkm.bmu.model.BatteryStatus
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

private class ControlFakeTransport(
    override val channel: TransportChannel,
    connected: Boolean,
    override val capabilities: Set<TransportCapability>,
    private var switchResult: CommandResult = CommandResult.ok(),
    private var resetResult: CommandResult = CommandResult.ok()
) : Transport {
    private val _isConnected = MutableStateFlow(connected)
    override val isConnected: StateFlow<Boolean> = _isConnected

    var switchCalls = 0
    var resetCalls = 0

    override fun observeBatteries(): Flow<List<BatteryState>> = flowOf(emptyList())
    override fun observeSystem(): Flow<SystemInfo?> = flowOf(null)
    override fun observeSolar(): Flow<SolarData?> = flowOf(null)

    override suspend fun switchBattery(index: Int, on: Boolean): CommandResult {
        switchCalls++
        return switchResult
    }

    override suspend fun resetSwitchCount(index: Int): CommandResult {
        resetCalls++
        return resetResult
    }

    override suspend fun setProtectionConfig(config: ProtectionConfig): CommandResult =
        CommandResult.error("Not used in this test")

    override suspend fun setWifiConfig(ssid: String, password: String): CommandResult =
        CommandResult.error("Not used in this test")

    override suspend fun connect() { _isConnected.value = true }
    override suspend fun disconnect() { _isConnected.value = false }
}

class ControlUseCaseTest {
    @Test
    fun switchBatteryReturnsErrorWhenCapabilityMissing() = runTest {
        val ble = ControlFakeTransport(
            channel = TransportChannel.BLE,
            connected = true,
            capabilities = setOf(TransportCapability.OBSERVE)
        )
        val manager = TransportManager(ble, null, null, ble)
        var auditCalls = 0
        val useCase = ControlUseCase(manager) { _, _, _ -> auditCalls++ }

        val result = useCase.switchBattery(index = 0, on = true)

        assertFalse(result.isSuccess)
        assertEquals(0, ble.switchCalls)
        assertEquals(0, auditCalls)
    }

    @Test
    fun switchBatterySuccessRecordsAudit() = runTest {
        val ble = ControlFakeTransport(
            channel = TransportChannel.BLE,
            connected = true,
            capabilities = setOf(TransportCapability.OBSERVE, TransportCapability.SWITCH_BATTERY)
        )
        val manager = TransportManager(ble, null, null, ble)
        var auditAction = ""
        val useCase = ControlUseCase(manager) { action, _, _ -> auditAction = action }

        val result = useCase.switchBattery(index = 1, on = true)

        assertTrue(result.isSuccess)
        assertEquals(1, ble.switchCalls)
        assertEquals("switch_on", auditAction)
    }

    @Test
    fun resetSwitchCountRequiresCapability() = runTest {
        val ble = ControlFakeTransport(
            channel = TransportChannel.BLE,
            connected = true,
            capabilities = setOf(TransportCapability.OBSERVE, TransportCapability.SWITCH_BATTERY)
        )
        val manager = TransportManager(ble, null, null, ble)
        val useCase = ControlUseCase(manager) { _, _, _ -> }

        val result = useCase.resetSwitchCount(index = 2)

        assertFalse(result.isSuccess)
        assertEquals(0, ble.resetCalls)
    }
}
