package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.kxkm.bmu.shared.domain.SohUseCase
import com.kxkm.bmu.shared.model.MlScore
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import javax.inject.Inject

@HiltViewModel
class SohDashboardViewModel @Inject constructor(
    private val sohUseCase: SohUseCase,
) : ViewModel() {

    private val _scores = MutableStateFlow<List<MlScore>>(emptyList())
    val scores: StateFlow<List<MlScore>> = _scores.asStateFlow()

    private val _isRefreshing = MutableStateFlow(false)
    val isRefreshing: StateFlow<Boolean> = _isRefreshing.asStateFlow()

    private val _lastRefreshMs = MutableStateFlow(0L)
    val lastRefreshMs: StateFlow<Long> = _lastRefreshMs.asStateFlow()

    // SOH history per battery (for sparklines), keyed by battery index
    private val _sohHistory = MutableStateFlow<Map<Int, List<MlScore>>>(emptyMap())
    val sohHistory: StateFlow<Map<Int, List<MlScore>>> = _sohHistory.asStateFlow()

    init {
        viewModelScope.launch {
            sohUseCase.observeMlScores { scores ->
                _scores.value = scores
            }
        }
        viewModelScope.launch {
            sohUseCase.lastRefreshMs.collect { _lastRefreshMs.value = it }
        }
        viewModelScope.launch {
            sohUseCase.isRefreshing.collect { _isRefreshing.value = it }
        }
    }

    fun refresh() {
        viewModelScope.launch {
            sohUseCase.refreshFromCloud()
        }
    }

    fun loadHistory(batteryIndex: Int) {
        viewModelScope.launch {
            val history = sohUseCase.getSohHistory(batteryIndex, days = 7)
            _sohHistory.value = _sohHistory.value + (batteryIndex to history)
        }
    }
}
