package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kxkm.bmu.shared.domain.MonitoringUseCase
import com.kxkm.bmu.shared.model.BatteryHealth
import com.kxkm.bmu.shared.model.BatteryState
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class DashboardViewModel @Inject constructor(
    private val monitoringUseCase: MonitoringUseCase,
) : ViewModel() {

    private val _batteries = MutableStateFlow<List<BatteryState>>(emptyList())
    val batteries: StateFlow<List<BatteryState>> = _batteries.asStateFlow()

    private val _isLoading = MutableStateFlow(true)
    val isLoading: StateFlow<Boolean> = _isLoading.asStateFlow()

    private val _healthMap = MutableStateFlow<Map<Int, BatteryHealth>>(emptyMap())
    val healthMap: StateFlow<Map<Int, BatteryHealth>> = _healthMap.asStateFlow()

    init {
        viewModelScope.launch {
            // Collect KMP StateFlow<List<BatteryState>>
            monitoringUseCase.observeBatteries { states ->
                _batteries.value = states
                _isLoading.value = false
            }
        }
        viewModelScope.launch {
            monitoringUseCase.observeHealth { healthList ->
                _healthMap.value = healthList.associateBy { it.index }
            }
        }
    }
}
