package com.kxkm.bmu.ui.auth

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Backspace
import androidx.compose.material.icons.filled.Fingerprint
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.kxkm.bmu.util.BiometricHelper
import com.kxkm.bmu.viewmodel.AuthViewModel

@Composable
fun PinEntryScreen(authVM: AuthViewModel) {
    var pin by remember { mutableStateOf("") }
    val pinError by authVM.pinError.collectAsState()
    val isAuthenticated by authVM.isAuthenticated.collectAsState()
    val context = LocalContext.current

    // Auto-submit when 6 digits entered
    LaunchedEffect(pin) {
        if (pin.length == 6) {
            authVM.login(pin)
            // If login failed, clear pin
            if (!isAuthenticated) pin = ""
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(32.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Icon(
            imageVector = Icons.Filled.Fingerprint,
            contentDescription = null,
            modifier = Modifier.size(64.dp),
            tint = MaterialTheme.colorScheme.primary,
        )

        Spacer(modifier = Modifier.height(16.dp))

        Text(
            text = "KXKM BMU",
            style = MaterialTheme.typography.headlineLarge,
        )

        Spacer(modifier = Modifier.height(8.dp))

        Text(
            text = "Entrez votre PIN",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        Spacer(modifier = Modifier.height(24.dp))

        // PIN dots
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            repeat(6) { i ->
                Box(
                    modifier = Modifier
                        .size(16.dp)
                        .clip(CircleShape)
                        .background(
                            if (i < pin.length) MaterialTheme.colorScheme.primary
                            else MaterialTheme.colorScheme.outlineVariant
                        ),
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Error message
        if (pinError != null) {
            Text(
                text = pinError ?: "",
                color = MaterialTheme.colorScheme.error,
                style = MaterialTheme.typography.bodySmall,
            )
        }

        Spacer(modifier = Modifier.height(32.dp))

        // Number pad
        LazyVerticalGrid(
            columns = GridCells.Fixed(3),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
            modifier = Modifier.padding(horizontal = 40.dp),
        ) {
            // 1-9
            items(9) { index ->
                val num = index + 1
                PinPadButton(label = "$num") {
                    if (pin.length < 6) pin += "$num"
                }
            }
            // Biometric
            item {
                PinPadButton(
                    icon = {
                        Icon(
                            Icons.Filled.Fingerprint,
                            contentDescription = "Biom\u00e9trie",
                        )
                    },
                ) {
                    BiometricHelper.authenticate(context) { success ->
                        if (success) {
                            val storedPin = BiometricHelper.getStoredPin(context)
                            if (storedPin != null) authVM.login(storedPin)
                        }
                    }
                }
            }
            // 0
            item {
                PinPadButton(label = "0") {
                    if (pin.length < 6) pin += "0"
                }
            }
            // Backspace
            item {
                PinPadButton(
                    icon = {
                        Icon(
                            Icons.AutoMirrored.Filled.Backspace,
                            contentDescription = "Effacer",
                        )
                    },
                ) {
                    if (pin.isNotEmpty()) pin = pin.dropLast(1)
                }
            }
        }
    }
}

@Composable
private fun PinPadButton(
    label: String? = null,
    icon: @Composable (() -> Unit)? = null,
    onClick: () -> Unit,
) {
    FilledTonalButton(
        onClick = onClick,
        modifier = Modifier.size(72.dp),
        shape = CircleShape,
    ) {
        if (icon != null) {
            icon()
        } else {
            Text(
                text = label ?: "",
                style = MaterialTheme.typography.titleLarge,
            )
        }
    }
}
