package com.kxkm.bmu.ui.detail

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.BoltOutlined
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Speed
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.shared.model.BatteryHealth
import com.kxkm.bmu.shared.model.BatteryState
import com.kxkm.bmu.ui.components.BatteryStateIcon
import com.kxkm.bmu.ui.components.ConfirmActionDialog
import com.kxkm.bmu.ui.theme.KxkmGreen
import com.kxkm.bmu.ui.theme.KxkmRed
import com.kxkm.bmu.util.ahDisplay
import com.kxkm.bmu.util.canControl
import com.kxkm.bmu.util.currentDisplay
import com.kxkm.bmu.util.displayName
import com.kxkm.bmu.util.voltageDisplay
import com.kxkm.bmu.viewmodel.AuthViewModel
import com.kxkm.bmu.viewmodel.BatteryDetailViewModel

@Composable
fun BatteryDetailScreen(
    batteryIndex: Int,
    authVM: AuthViewModel,
    viewModel: BatteryDetailViewModel = hiltViewModel(),
) {
    val battery by viewModel.battery.collectAsState()
    val history by viewModel.history.collectAsState()
    val commandResult by viewModel.commandResult.collectAsState()
    val currentUser by authVM.currentUser.collectAsState()

    var showConfirmDialog by remember { mutableStateOf(false) }
    var pendingSwitchOn by remember { mutableStateOf(true) }

    if (showConfirmDialog) {
        ConfirmActionDialog(
            title = if (pendingSwitchOn) "Connecter batterie ?" else "D\u00e9connecter batterie ?",
            message = "Batterie ${batteryIndex + 1} \u2014 cette action est enregistr\u00e9e dans l'audit.",
            onConfirm = { viewModel.switchBattery(pendingSwitchOn) },
            onDismiss = { showConfirmDialog = false },
        )
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text(
            text = "Batterie ${batteryIndex + 1}",
            style = MaterialTheme.typography.headlineLarge,
        )

        // State card
        battery?.let { bat -> StateCard(bat) }

        // Health card (SOH + R_int)
        val health by viewModel.health.collectAsState()
        val rintMeasuring by viewModel.rintMeasuring.collectAsState()

        health?.let { h -> HealthCard(h, rintMeasuring, onTriggerRint = { viewModel.triggerRintMeasurement() }) }

        // Chart
        Card(
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Historique tension (24h)",
                    style = MaterialTheme.typography.titleMedium,
                )
                Spacer(modifier = Modifier.height(8.dp))
                VoltageChart(
                    history = history,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(200.dp),
                )
            }
        }

        // Counters
        battery?.let { bat -> CountersCard(bat) }

        // Controls (role-gated)
        if (currentUser?.role?.canControl == true) {
            ControlsCard(
                onConnect = {
                    pendingSwitchOn = true
                    showConfirmDialog = true
                },
                onDisconnect = {
                    pendingSwitchOn = false
                    showConfirmDialog = true
                },
                onReset = { viewModel.resetSwitchCount() },
            )
        }

        // Command result
        commandResult?.let { result ->
            Text(
                text = result,
                style = MaterialTheme.typography.bodySmall,
                color = if (result == "OK" || result.startsWith("Compteur"))
                    MaterialTheme.colorScheme.primary
                else MaterialTheme.colorScheme.error,
            )
        }
    }
}

@Composable
private fun StateCard(battery: BatteryState) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column {
                Text(
                    text = battery.voltageMv.voltageDisplay(),
                    style = MaterialTheme.typography.headlineLarge,
                    fontFamily = FontFamily.Monospace,
                )
                Text(
                    text = battery.currentMa.currentDisplay(),
                    style = MaterialTheme.typography.titleMedium,
                    fontFamily = FontFamily.Monospace,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                BatteryStateIcon(state = battery.state)
                Text(
                    text = battery.state.displayName,
                    style = MaterialTheme.typography.labelSmall,
                )
            }
        }
    }
}

@Composable
private fun CountersCard(battery: BatteryState) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                text = "Compteurs",
                style = MaterialTheme.typography.titleMedium,
            )
            CounterRow("D\u00e9charge", battery.ahDischargeMah.ahDisplay())
            CounterRow("Charge", battery.ahChargeMah.ahDisplay())
            CounterRow("Nb switches", "${battery.nbSwitch}")
        }
    }
}

@Composable
private fun CounterRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(text = label, style = MaterialTheme.typography.bodyMedium)
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            fontFamily = FontFamily.Monospace,
        )
    }
}

@Composable
private fun ControlsCard(
    onConnect: () -> Unit,
    onDisconnect: () -> Unit,
    onReset: () -> Unit,
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                text = "Contr\u00f4le",
                style = MaterialTheme.typography.titleMedium,
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Button(
                    onClick = onConnect,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(containerColor = KxkmGreen),
                ) {
                    Icon(Icons.Filled.Bolt, contentDescription = null)
                    Text("Connecter", modifier = Modifier.padding(start = 4.dp))
                }
                Button(
                    onClick = onDisconnect,
                    modifier = Modifier.weight(1f),
                    colors = ButtonDefaults.buttonColors(containerColor = KxkmRed),
                ) {
                    Icon(Icons.Filled.BoltOutlined, contentDescription = null)
                    Text("D\u00e9connecter", modifier = Modifier.padding(start = 4.dp))
                }
            }
            OutlinedButton(
                onClick = onReset,
                modifier = Modifier.fillMaxWidth(),
            ) {
                Icon(Icons.Filled.Refresh, contentDescription = null)
                Text("Reset compteur", modifier = Modifier.padding(start = 4.dp))
            }
        }
    }
}

@Composable
private fun HealthCard(
    health: BatteryHealth,
    measuring: Boolean,
    onTriggerRint: () -> Unit,
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = "Sante batterie",
                style = MaterialTheme.typography.titleMedium,
            )

            // SOH gauge row
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column {
                    Text(
                        text = "SOH",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Text(
                        text = if (health.sohPercent > 0) "${health.sohPercent}%" else "--",
                        style = MaterialTheme.typography.headlineMedium,
                        fontFamily = FontFamily.Monospace,
                    )
                }
                Column {
                    Text(
                        text = "Confiance",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Text(
                        text = "${health.sohConfidence}%",
                        style = MaterialTheme.typography.titleMedium,
                        fontFamily = FontFamily.Monospace,
                    )
                }
            }

            // R_int values
            if (health.rintValid) {
                CounterRow("R ohmic", "%.1f m\u03A9".format(health.rOhmicMohm))
                CounterRow("R total", "%.1f m\u03A9".format(health.rTotalMohm))
            } else {
                Text(
                    text = "R_int non disponible",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            // Trigger R_int button
            OutlinedButton(
                onClick = onTriggerRint,
                modifier = Modifier.fillMaxWidth(),
                enabled = !measuring,
            ) {
                if (measuring) {
                    Text("Mesure en cours...")
                } else {
                    Icon(Icons.Filled.Speed, contentDescription = null)
                    Text("Mesurer R_int", modifier = Modifier.padding(start = 4.dp))
                }
            }
        }
    }
}
