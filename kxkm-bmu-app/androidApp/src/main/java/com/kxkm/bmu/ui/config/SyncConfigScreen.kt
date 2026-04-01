package com.kxkm.bmu.ui.config

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.viewmodel.ConfigViewModel
import java.text.DateFormat
import java.util.Date

@Composable
fun SyncConfigScreen(
    viewModel: ConfigViewModel = hiltViewModel(),
) {
    val syncUrl by viewModel.syncUrl.collectAsState()
    val mqttBroker by viewModel.mqttBroker.collectAsState()
    val syncPending by viewModel.syncPending.collectAsState()
    val lastSyncTime by viewModel.lastSyncTime.collectAsState()

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Sync cloud", style = MaterialTheme.typography.headlineLarge)

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("kxkm-ai", style = MaterialTheme.typography.titleMedium)

                OutlinedTextField(
                    value = syncUrl,
                    onValueChange = { viewModel.syncUrl.value = it },
                    label = { Text("URL API") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri),
                    modifier = Modifier.fillMaxWidth(),
                )

                OutlinedTextField(
                    value = mqttBroker,
                    onValueChange = { viewModel.mqttBroker.value = it },
                    label = { Text("Broker MQTT") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text("\u00c9tat", style = MaterialTheme.typography.titleMedium)

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text("En attente de sync")
                    Text(
                        text = "$syncPending",
                        color = if (syncPending > 0) MaterialTheme.colorScheme.tertiary
                        else MaterialTheme.colorScheme.primary,
                    )
                }

                lastSyncTime?.let { time ->
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                    ) {
                        Text("Dernier sync")
                        Text(
                            text = DateFormat.getDateTimeInstance(
                                DateFormat.SHORT, DateFormat.SHORT,
                            ).format(time),
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }
    }
}
