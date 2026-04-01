package com.kxkm.bmu.domain

import com.kxkm.bmu.model.CommandResult
import com.kxkm.bmu.sync.AuditUseCase
import com.kxkm.bmu.transport.TransportManager
import kotlinx.coroutines.*

class ControlUseCase(
    private val transport: TransportManager,
    private val audit: AuditUseCase,
    private val currentUserId: () -> String
) {
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())

    fun switchBattery(index: Int, on: Boolean, callback: (CommandResult) -> Unit) {
        scope.launch {
            val result = transport.switchBattery(index, on)
            if (result.isSuccess) {
                audit.record(currentUserId(), if (on) "switch_on" else "switch_off", index)
            }
            callback(result)
        }
    }

    fun resetSwitchCount(index: Int, callback: (CommandResult) -> Unit) {
        scope.launch {
            val result = transport.resetSwitchCount(index)
            if (result.isSuccess) {
                audit.record(currentUserId(), "reset", index)
            }
            callback(result)
        }
    }
}
