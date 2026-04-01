package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import com.kxkm.bmu.shared.auth.AuthUseCase
import com.kxkm.bmu.shared.domain.ConfigUseCase
import com.kxkm.bmu.shared.model.TransportChannel
import com.kxkm.bmu.shared.model.UserProfile
import com.kxkm.bmu.shared.model.UserRole
import com.kxkm.bmu.shared.model.WifiStatusInfo
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.Date
import javax.inject.Inject

@HiltViewModel
class ConfigViewModel @Inject constructor(
    private val configUseCase: ConfigUseCase,
    private val authUseCase: AuthUseCase,
) : ViewModel() {

    // Protection
    val minMv = MutableStateFlow(24000)
    val maxMv = MutableStateFlow(30000)
    val maxMa = MutableStateFlow(10000)
    val diffMv = MutableStateFlow(1000)

    // WiFi
    val wifiSsid = MutableStateFlow("")
    val wifiPassword = MutableStateFlow("")
    private val _wifiStatus = MutableStateFlow<WifiStatusInfo?>(null)
    val wifiStatus: StateFlow<WifiStatusInfo?> = _wifiStatus.asStateFlow()

    // Users
    private val _users = MutableStateFlow<List<UserProfile>>(emptyList())
    val users: StateFlow<List<UserProfile>> = _users.asStateFlow()

    // Sync
    val syncUrl = MutableStateFlow("")
    val mqttBroker = MutableStateFlow("")
    private val _syncPending = MutableStateFlow(0)
    val syncPending: StateFlow<Int> = _syncPending.asStateFlow()
    private val _lastSyncTime = MutableStateFlow<Date?>(null)
    val lastSyncTime: StateFlow<Date?> = _lastSyncTime.asStateFlow()

    // Transport
    val activeChannel = MutableStateFlow(TransportChannel.OFFLINE)
    val forceChannel = MutableStateFlow<TransportChannel?>(null)

    private val _statusMessage = MutableStateFlow<String?>(null)
    val statusMessage: StateFlow<String?> = _statusMessage.asStateFlow()

    init {
        loadAll()
    }

    fun loadAll() {
        val cfg = configUseCase.getCurrentConfig()
        minMv.value = cfg.minMv
        maxMv.value = cfg.maxMv
        maxMa.value = cfg.maxMa
        diffMv.value = cfg.diffMv

        _users.value = authUseCase.getAllUsers()
        _syncPending.value = configUseCase.getPendingSyncCount()
    }

    fun saveProtection() {
        configUseCase.setProtectionConfig(
            minMv = minMv.value,
            maxMv = maxMv.value,
            maxMa = maxMa.value,
            diffMv = diffMv.value,
        ) { result ->
            _statusMessage.value = if (result.isSuccess) "Seuils mis \u00e0 jour" else "Erreur"
        }
    }

    fun sendWifiConfig() {
        configUseCase.setWifiConfig(
            ssid = wifiSsid.value,
            password = wifiPassword.value,
        ) { result ->
            _statusMessage.value = if (result.isSuccess) "WiFi configur\u00e9" else "Erreur (BLE requis)"
        }
    }

    fun deleteUser(user: UserProfile) {
        authUseCase.deleteUser(user.id)
        _users.value = authUseCase.getAllUsers()
    }

    fun createUser(name: String, pin: String, role: UserRole) {
        authUseCase.createUser(name, pin, role)
        _users.value = authUseCase.getAllUsers()
    }
}
