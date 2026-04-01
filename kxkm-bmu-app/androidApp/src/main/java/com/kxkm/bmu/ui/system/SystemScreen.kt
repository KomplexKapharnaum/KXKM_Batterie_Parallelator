package com.kxkm.bmu.ui.system

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Cancel
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.util.formatBytes
import com.kxkm.bmu.util.formatUptime
import com.kxkm.bmu.viewmodel.SystemViewModel

@Composable
fun SystemScreen(
    viewModel: SystemViewModel = hiltViewModel(),
) {
    val system by viewModel.system.collectAsState()
    val solar by viewModel.solar.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(
            text = "Syst\u00e8me",
            style = MaterialTheme.typography.headlineLarge,
        )

        val sys = system
        if (sys != null) {
            // Firmware card
            SectionCard("Firmware") {
                InfoRow("Version", sys.firmwareVersion)
                InfoRow("Uptime", sys.uptimeSeconds.formatUptime())
                InfoRow("Heap libre", sys.heapFree.formatBytes())
            }

            // Topology card
            SectionCard("Topologie") {
                InfoRow("INA237", "${sys.nbIna}")
                InfoRow("TCA9535", "${sys.nbTca}")
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text("Validation", style = MaterialTheme.typography.bodyMedium)
                    Icon(
                        imageVector = if (sys.topologyValid) Icons.Filled.CheckCircle
                        else Icons.Filled.Cancel,
                        contentDescription = null,
                        tint = if (sys.topologyValid) MaterialTheme.colorScheme.primary
                        else MaterialTheme.colorScheme.error,
                    )
                }
            }

            // WiFi card
            SectionCard("WiFi") {
                InfoRow("IP", sys.wifiIp ?: "Non connect\u00e9")
            }
        } else {
            CircularProgressIndicator(modifier = Modifier.align(Alignment.CenterHorizontally))
        }

        // Solar card
        solar?.let { sol ->
            SectionCard("Solaire (VE.Direct)") {
                InfoRow("Tension panneau", "%.1f V".format(sol.panelVoltageMv / 1000.0))
                InfoRow("Puissance", "${sol.panelPowerW} W")
                InfoRow("Tension batterie", "%.1f V".format(sol.batteryVoltageMv / 1000.0))
                InfoRow("Courant", "%.2f A".format(sol.batteryCurrentMa / 1000.0))
                InfoRow("\u00c9tat charge", chargeStateName(sol.chargeState))
                InfoRow("Production jour", "${sol.yieldTodayWh} Wh")
            }
        }
    }
}

@Composable
private fun SectionCard(
    title: String,
    content: @Composable () -> Unit,
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(text = title, style = MaterialTheme.typography.titleMedium)
            content()
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(text = label, style = MaterialTheme.typography.bodyMedium)
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            fontFamily = FontFamily.Monospace,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

private fun chargeStateName(cs: Int): String = when (cs) {
    0 -> "Off"
    2 -> "Fault"
    3 -> "Bulk"
    4 -> "Absorption"
    5 -> "Float"
    else -> "\u00c9tat $cs"
}
