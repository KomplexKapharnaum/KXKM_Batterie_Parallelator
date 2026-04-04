package com.kxkm.bmu.domain

import com.kxkm.bmu.model.*
import com.kxkm.bmu.sync.AuditUseCase
import com.kxkm.bmu.transport.TransportCapability
import com.kxkm.bmu.transport.TransportManager
import kotlinx.coroutines.*

class ConfigUseCase(
    private val transport: TransportManager,
    private val recordAudit: (action: String, target: Int?, detail: String?) -> Unit
) {
    constructor(
        transport: TransportManager,
        audit: AuditUseCase,
        currentUserId: () -> String
    ) : this(
        transport,
        recordAudit = { action, target, detail ->
            audit.record(currentUserId(), action, target, detail)
        }
    )

    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    fun getCurrentConfig(): ProtectionConfig {
        // Read from local cache or defaults
        return ProtectionConfig()
    }

    suspend fun setProtectionConfig(minMv: Int, maxMv: Int, maxMa: Int, diffMv: Int): CommandResult {
        if (!transport.supports(TransportCapability.SET_CONFIG)) {
            return CommandResult.error("Configuration indisponible sur ce transport")
        }
        val config = ProtectionConfig(minMv, maxMv, maxMa, diffMv)
        val result = transport.setProtectionConfig(config)
        if (result.isSuccess) {
            recordAudit("config_change", null,
                "min=$minMv max=$maxMv maxI=$maxMa diff=$diffMv")
        }
        return result
    }

    suspend fun setWifiConfig(ssid: String, password: String): CommandResult {
        if (!transport.supports(TransportCapability.SET_WIFI)) {
            return CommandResult.error("Config WiFi indisponible sur ce transport")
        }
        val result = transport.setWifiConfig(ssid, password)
        if (result.isSuccess) {
            recordAudit("wifi_config", null, "ssid=$ssid")
        }
        return result
    }

    fun setProtectionConfig(minMv: Int, maxMv: Int, maxMa: Int, diffMv: Int,
                            callback: (CommandResult) -> Unit) {
        scope.launch {
            callback(setProtectionConfig(minMv, maxMv, maxMa, diffMv))
        }
    }

    fun setWifiConfig(ssid: String, password: String, callback: (CommandResult) -> Unit) {
        scope.launch {
            callback(setWifiConfig(ssid, password))
        }
    }

    fun getPendingSyncCount(): Long = 0 // Delegated to SyncManager

    fun close() {
        scope.cancel()
    }
}
