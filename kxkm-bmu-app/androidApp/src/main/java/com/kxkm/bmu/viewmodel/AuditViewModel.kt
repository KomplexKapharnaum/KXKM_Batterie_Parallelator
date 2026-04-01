package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import com.kxkm.bmu.shared.domain.AuditUseCase
import com.kxkm.bmu.shared.model.AuditEvent
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import javax.inject.Inject

@HiltViewModel
class AuditViewModel @Inject constructor(
    private val auditUseCase: AuditUseCase,
) : ViewModel() {

    private val _events = MutableStateFlow<List<AuditEvent>>(emptyList())
    val events: StateFlow<List<AuditEvent>> = _events.asStateFlow()

    private val _filterAction = MutableStateFlow<String?>(null)
    val filterAction: StateFlow<String?> = _filterAction.asStateFlow()

    private val _filterBattery = MutableStateFlow<Int?>(null)
    val filterBattery: StateFlow<Int?> = _filterBattery.asStateFlow()

    private val _pendingSyncCount = MutableStateFlow(0)
    val pendingSyncCount: StateFlow<Int> = _pendingSyncCount.asStateFlow()

    init {
        reload()
    }

    fun reload() {
        auditUseCase.getEvents(
            action = _filterAction.value,
            batteryIndex = _filterBattery.value,
        ) { result ->
            _events.value = result
        }
        _pendingSyncCount.value = auditUseCase.getPendingSyncCount()
    }

    fun setFilterAction(action: String?) {
        _filterAction.value = action
        reload()
    }

    fun clearFilters() {
        _filterAction.value = null
        _filterBattery.value = null
        reload()
    }
}
