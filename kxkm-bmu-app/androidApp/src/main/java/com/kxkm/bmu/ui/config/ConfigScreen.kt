package com.kxkm.bmu.ui.config

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

@Composable
fun ConfigScreen(
    onNavigateProtection: () -> Unit,
    onNavigateWifi: () -> Unit,
    onNavigateUsers: () -> Unit,
    onNavigateSync: () -> Unit,
    onNavigateTransport: () -> Unit,
) {
    Column(modifier = Modifier.fillMaxSize()) {
        Text(
            text = "Configuration",
            style = MaterialTheme.typography.headlineLarge,
            modifier = Modifier.padding(16.dp),
        )

        ConfigMenuItem("Protection", onNavigateProtection)
        HorizontalDivider()
        ConfigMenuItem("WiFi BMU", onNavigateWifi)
        HorizontalDivider()
        ConfigMenuItem("Utilisateurs", onNavigateUsers)
        HorizontalDivider()
        ConfigMenuItem("Sync cloud", onNavigateSync)
        HorizontalDivider()
        ConfigMenuItem("Transport", onNavigateTransport)
    }
}

@Composable
private fun ConfigMenuItem(title: String, onClick: () -> Unit) {
    ListItem(
        headlineContent = { Text(title) },
        trailingContent = {
            Icon(
                imageVector = Icons.AutoMirrored.Filled.ArrowForward,
                contentDescription = null,
            )
        },
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick),
    )
}
