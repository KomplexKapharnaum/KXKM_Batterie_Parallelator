package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kxkm.bmu.shared.domain.MonitoringUseCase
import com.kxkm.bmu.shared.model.SolarData
import com.kxkm.bmu.shared.model.SystemInfo
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class SystemViewModel @Inject constructor(
    private val monitoringUseCase: MonitoringUseCase,
) : ViewModel() {

    private val _system = MutableStateFlow<SystemInfo?>(null)
    val system: StateFlow<SystemInfo?> = _system.asStateFlow()

    private val _solar = MutableStateFlow<SolarData?>(null)
    val solar: StateFlow<SolarData?> = _solar.asStateFlow()

    init {
        viewModelScope.launch {
            monitoringUseCase.observeSystem { info ->
                _system.value = info
            }
        }
        viewModelScope.launch {
            monitoringUseCase.observeSolar { data ->
                _solar.value = data
            }
        }
    }
}
