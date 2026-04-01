package com.kxkm.bmu.domain

import com.kxkm.bmu.model.*
import com.kxkm.bmu.sync.AuditUseCase
import com.kxkm.bmu.transport.TransportManager
import kotlinx.coroutines.*

class ConfigUseCase(
    private val transport: TransportManager,
    private val audit: AuditUseCase,
    private val currentUserId: () -> String
) {
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    fun getCurrentConfig(): ProtectionConfig {
        // Read from local cache or defaults
        return ProtectionConfig()
    }

    fun setProtectionConfig(minMv: Int, maxMv: Int, maxMa: Int, diffMv: Int,
                            callback: (CommandResult) -> Unit) {
        scope.launch {
            val config = ProtectionConfig(minMv, maxMv, maxMa, diffMv)
            val result = transport.setProtectionConfig(config)
            if (result.isSuccess) {
                audit.record(currentUserId(), "config_change", null,
                    "min=$minMv max=$maxMv maxI=$maxMa diff=$diffMv")
            }
            callback(result)
        }
    }

    fun setWifiConfig(ssid: String, password: String, callback: (CommandResult) -> Unit) {
        scope.launch {
            val result = transport.setWifiConfig(ssid, password)
            if (result.isSuccess) {
                audit.record(currentUserId(), "wifi_config", null, "ssid=$ssid")
            }
            callback(result)
        }
    }

    fun getPendingSyncCount(): Long = 0 // Delegated to SyncManager
}
