package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.emptyFlow

/** Unified transport interface — implemented by BLE, WiFi, MQTT, REST, Offline */
interface Transport {
    val channel: TransportChannel
    val isConnected: StateFlow<Boolean>

    /** Reactive battery state stream */
    fun observeBatteries(): Flow<List<BatteryState>>
    fun observeSystem(): Flow<SystemInfo?>
    fun observeSolar(): Flow<SolarData?>

    /** Commands (BLE/WiFi only — throws on read-only transports) */
    suspend fun switchBattery(index: Int, on: Boolean): CommandResult
    suspend fun resetSwitchCount(index: Int): CommandResult
    suspend fun setProtectionConfig(config: ProtectionConfig): CommandResult
    suspend fun setWifiConfig(ssid: String, password: String): CommandResult

    /** Reactive battery health stream (SOH + R_int summary) */
    fun observeHealth(): Flow<List<BatteryHealth>> = emptyFlow()

    /** Trigger R_int measurement on a specific battery (or all with index=-1) */
    suspend fun triggerRintMeasurement(batteryIndex: Int): CommandResult =
        CommandResult.error("Not supported")

    /** Connection lifecycle */
    suspend fun connect()
    suspend fun disconnect()
}
