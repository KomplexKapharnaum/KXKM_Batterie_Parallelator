package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kxkm.bmu.shared.domain.SohUseCase
import com.kxkm.bmu.shared.model.FleetHealth
import com.kxkm.bmu.shared.model.MlScore
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class FleetViewModel @Inject constructor(
    private val sohUseCase: SohUseCase,
) : ViewModel() {

    private val _fleetHealth = MutableStateFlow<FleetHealth?>(null)
    val fleetHealth: StateFlow<FleetHealth?> = _fleetHealth.asStateFlow()

    private val _scores = MutableStateFlow<List<MlScore>>(emptyList())
    val scores: StateFlow<List<MlScore>> = _scores.asStateFlow()

    init {
        viewModelScope.launch {
            sohUseCase.observeFleetHealth { _fleetHealth.value = it }
        }
        viewModelScope.launch {
            sohUseCase.observeMlScores { _scores.value = it }
        }
    }

    fun refresh() {
        viewModelScope.launch { sohUseCase.refreshFromCloud() }
    }
}
