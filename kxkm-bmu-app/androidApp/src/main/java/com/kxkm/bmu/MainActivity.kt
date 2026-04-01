package com.kxkm.bmu

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.ui.BmuNavHost
import com.kxkm.bmu.ui.auth.OnboardingScreen
import com.kxkm.bmu.ui.auth.PinEntryScreen
import com.kxkm.bmu.ui.theme.BmuTheme
import com.kxkm.bmu.viewmodel.AuthViewModel
import dagger.hilt.android.AndroidEntryPoint

@AndroidEntryPoint
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            BmuTheme {
                val authVM: AuthViewModel = hiltViewModel()
                val isAuthenticated by authVM.isAuthenticated.collectAsState()
                val needsOnboarding by authVM.needsOnboarding.collectAsState()

                when {
                    needsOnboarding -> OnboardingScreen(authVM = authVM)
                    !isAuthenticated -> PinEntryScreen(authVM = authVM)
                    else -> BmuNavHost(authVM = authVM)
                }
            }
        }
    }
}
