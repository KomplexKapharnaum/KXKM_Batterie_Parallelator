package com.kxkm.bmu.viewmodel

import androidx.lifecycle.SavedStateHandle
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kxkm.bmu.shared.domain.ControlUseCase
import com.kxkm.bmu.shared.domain.MonitoringUseCase
import com.kxkm.bmu.shared.model.BatteryHealth
import com.kxkm.bmu.shared.model.BatteryHistoryPoint
import com.kxkm.bmu.shared.model.BatteryState
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class BatteryDetailViewModel @Inject constructor(
    savedStateHandle: SavedStateHandle,
    private val monitoringUseCase: MonitoringUseCase,
    private val controlUseCase: ControlUseCase,
) : ViewModel() {

    val batteryIndex: Int = savedStateHandle.get<Int>("batteryIndex") ?: 0

    private val _battery = MutableStateFlow<BatteryState?>(null)
    val battery: StateFlow<BatteryState?> = _battery.asStateFlow()

    private val _history = MutableStateFlow<List<BatteryHistoryPoint>>(emptyList())
    val history: StateFlow<List<BatteryHistoryPoint>> = _history.asStateFlow()

    private val _commandResult = MutableStateFlow<String?>(null)
    val commandResult: StateFlow<String?> = _commandResult.asStateFlow()

    private val _health = MutableStateFlow<BatteryHealth?>(null)
    val health: StateFlow<BatteryHealth?> = _health.asStateFlow()

    private val _rintMeasuring = MutableStateFlow(false)
    val rintMeasuring: StateFlow<Boolean> = _rintMeasuring.asStateFlow()

    init {
        viewModelScope.launch {
            monitoringUseCase.observeBattery(batteryIndex) { state ->
                _battery.value = state
            }
        }
        viewModelScope.launch {
            monitoringUseCase.observeHealth(batteryIndex) { h ->
                _health.value = h
            }
        }
        loadHistory()
    }

    private fun loadHistory() {
        viewModelScope.launch {
            monitoringUseCase.getHistory(batteryIndex, hours = 24) { points ->
                _history.value = points
            }
        }
    }

    fun switchBattery(on: Boolean) {
        viewModelScope.launch {
            controlUseCase.switchBattery(batteryIndex, on) { result ->
                _commandResult.value =
                    if (result.isSuccess) "OK" else "Erreur: ${result.errorMessage ?: ""}"
            }
        }
    }

    fun triggerRintMeasurement() {
        viewModelScope.launch {
            _rintMeasuring.value = true
            val result = monitoringUseCase.triggerRintMeasurement(batteryIndex)
            _commandResult.value = if (result.isSuccess) "R_int mesure lancee"
                else "Erreur: ${result.errorMessage ?: ""}"
            _rintMeasuring.value = false
        }
    }

    fun resetSwitchCount() {
        viewModelScope.launch {
            controlUseCase.resetSwitchCount(batteryIndex) { result ->
                _commandResult.value =
                    if (result.isSuccess) "Compteur remis \u00e0 z\u00e9ro" else "Erreur"
            }
        }
    }
}
