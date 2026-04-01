package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import com.kxkm.bmu.shared.model.TransportChannel
import com.kxkm.bmu.shared.transport.TransportManager
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import javax.inject.Inject

@HiltViewModel
class TransportStatusViewModel @Inject constructor(
    private val transportManager: TransportManager,
) : ViewModel() {

    // These StateFlows will be wired to TransportManager's flows
    // when Plan 2 defines the exact Flow API
    val channel: StateFlow<TransportChannel> = MutableStateFlow(TransportChannel.OFFLINE).asStateFlow()
    val isConnected: StateFlow<Boolean> = MutableStateFlow(false).asStateFlow()
    val deviceName: StateFlow<String?> = MutableStateFlow<String?>(null).asStateFlow()
    val rssi: StateFlow<Int?> = MutableStateFlow<Int?>(null).asStateFlow()
}
