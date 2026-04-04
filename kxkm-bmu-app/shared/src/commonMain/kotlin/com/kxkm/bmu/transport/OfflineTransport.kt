package com.kxkm.bmu.transport

import com.kxkm.bmu.model.*
import com.kxkm.bmu.db.DatabaseHelper
import kotlinx.coroutines.flow.*

/** Offline transport — reads from local SQLDelight cache, no commands */
class OfflineTransport(private val db: DatabaseHelper) : Transport {
    override val channel = TransportChannel.OFFLINE
    override val isConnected: StateFlow<Boolean> = MutableStateFlow(false)
    override val capabilities = setOf(TransportCapability.OBSERVE)

    override fun observeBatteries(): Flow<List<BatteryState>> = flowOf(db.getLastKnownBatteries())
    override fun observeSystem(): Flow<SystemInfo?> = flowOf(null)
    override fun observeSolar(): Flow<SolarData?> = flowOf(null)

    override suspend fun connect() {}
    override suspend fun disconnect() {}

    override suspend fun switchBattery(index: Int, on: Boolean) =
        CommandResult.error("Offline — pas de contrôle")
    override suspend fun resetSwitchCount(index: Int) =
        CommandResult.error("Offline — pas de contrôle")
    override suspend fun setProtectionConfig(config: ProtectionConfig) =
        CommandResult.error("Offline — pas de contrôle")
    override suspend fun setWifiConfig(ssid: String, password: String) =
        CommandResult.error("Offline — pas de contrôle")
}
