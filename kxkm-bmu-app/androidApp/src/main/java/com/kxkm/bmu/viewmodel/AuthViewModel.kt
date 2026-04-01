package com.kxkm.bmu.viewmodel

import androidx.lifecycle.ViewModel
import com.kxkm.bmu.shared.auth.AuthUseCase
import com.kxkm.bmu.shared.model.UserProfile
import com.kxkm.bmu.shared.model.UserRole
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import javax.inject.Inject

@HiltViewModel
class AuthViewModel @Inject constructor(
    private val authUseCase: AuthUseCase,
) : ViewModel() {

    private val _isAuthenticated = MutableStateFlow(false)
    val isAuthenticated: StateFlow<Boolean> = _isAuthenticated.asStateFlow()

    private val _needsOnboarding = MutableStateFlow(authUseCase.hasNoUsers())
    val needsOnboarding: StateFlow<Boolean> = _needsOnboarding.asStateFlow()

    private val _currentUser = MutableStateFlow<UserProfile?>(null)
    val currentUser: StateFlow<UserProfile?> = _currentUser.asStateFlow()

    private val _pinError = MutableStateFlow<String?>(null)
    val pinError: StateFlow<String?> = _pinError.asStateFlow()

    fun login(pin: String) {
        val user = authUseCase.authenticate(pin)
        if (user != null) {
            _currentUser.value = user
            _isAuthenticated.value = true
            _pinError.value = null
        } else {
            _pinError.value = "PIN incorrect"
        }
    }

    fun createAdmin(name: String, pin: String) {
        authUseCase.createUser(name, pin, UserRole.ADMIN)
        _needsOnboarding.value = false
    }

    fun logout() {
        _isAuthenticated.value = false
        _currentUser.value = null
    }
}
