package com.kxkm.bmu.domain

import com.kxkm.bmu.model.CommandResult
import com.kxkm.bmu.sync.AuditUseCase
import com.kxkm.bmu.transport.TransportCapability
import com.kxkm.bmu.transport.TransportManager
import kotlinx.coroutines.*

class ControlUseCase(
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

    suspend fun switchBattery(index: Int, on: Boolean): CommandResult {
        if (!transport.supports(TransportCapability.SWITCH_BATTERY)) {
            return CommandResult.error("Commande indisponible sur ce transport")
        }
        val result = transport.switchBattery(index, on)
        if (result.isSuccess) {
            recordAudit(if (on) "switch_on" else "switch_off", index, null)
        }
        return result
    }

    suspend fun resetSwitchCount(index: Int): CommandResult {
        if (!transport.supports(TransportCapability.RESET_SWITCH)) {
            return CommandResult.error("Reset indisponible sur ce transport")
        }
        val result = transport.resetSwitchCount(index)
        if (result.isSuccess) {
            recordAudit("reset", index, null)
        }
        return result
    }

    fun switchBattery(index: Int, on: Boolean, callback: (CommandResult) -> Unit) {
        scope.launch {
            callback(switchBattery(index, on))
        }
    }

    fun resetSwitchCount(index: Int, callback: (CommandResult) -> Unit) {
        scope.launch {
            callback(resetSwitchCount(index))
        }
    }

    fun close() {
        scope.cancel()
    }
}
