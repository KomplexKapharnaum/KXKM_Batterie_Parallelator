package com.kxkm.bmu.ui.config

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Remove
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import com.kxkm.bmu.viewmodel.ConfigViewModel

@Composable
fun ProtectionConfigScreen(
    viewModel: ConfigViewModel = hiltViewModel(),
) {
    val minMv by viewModel.minMv.collectAsState()
    val maxMv by viewModel.maxMv.collectAsState()
    val maxMa by viewModel.maxMa.collectAsState()
    val diffMv by viewModel.diffMv.collectAsState()
    val statusMessage by viewModel.statusMessage.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Protection", style = MaterialTheme.typography.headlineLarge)

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("Seuils de tension", style = MaterialTheme.typography.titleMedium)
                StepperRow("V min", minMv, "mV", step = 500, range = 20000..30000) {
                    viewModel.minMv.value = it
                }
                StepperRow("V max", maxMv, "mV", step = 500, range = 25000..35000) {
                    viewModel.maxMv.value = it
                }
                StepperRow("V diff max", diffMv, "mV", step = 100, range = 100..5000) {
                    viewModel.diffMv.value = it
                }
            }
        }

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("Seuil de courant", style = MaterialTheme.typography.titleMedium)
                StepperRow("I max", maxMa, "mA", step = 1000, range = 1000..50000) {
                    viewModel.maxMa.value = it
                }
            }
        }

        Button(
            onClick = { viewModel.saveProtection() },
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("Envoyer au BMU")
        }

        statusMessage?.let {
            Text(
                text = it,
                color = MaterialTheme.colorScheme.primary,
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }
}

@Composable
private fun StepperRow(
    label: String,
    value: Int,
    unit: String,
    step: Int,
    range: IntRange,
    onValueChange: (Int) -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(text = label, style = MaterialTheme.typography.bodyMedium)
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconButton(
                onClick = { if (value - step >= range.first) onValueChange(value - step) },
            ) {
                Icon(Icons.Filled.Remove, contentDescription = "Diminuer")
            }
            Text(
                text = "$value $unit",
                style = MaterialTheme.typography.bodyMedium,
                fontFamily = FontFamily.Monospace,
            )
            IconButton(
                onClick = { if (value + step <= range.last) onValueChange(value + step) },
            ) {
                Icon(Icons.Filled.Add, contentDescription = "Augmenter")
            }
        }
    }
}
