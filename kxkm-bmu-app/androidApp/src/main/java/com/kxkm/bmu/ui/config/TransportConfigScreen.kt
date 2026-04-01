package com.kxkm.bmu.ui.config

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.shared.model.TransportChannel
import com.kxkm.bmu.util.displayName
import com.kxkm.bmu.util.icon
import com.kxkm.bmu.viewmodel.ConfigViewModel

@Composable
fun TransportConfigScreen(
    viewModel: ConfigViewModel = hiltViewModel(),
) {
    val activeChannel by viewModel.activeChannel.collectAsState()
    val forceChannel by viewModel.forceChannel.collectAsState()

    val channelOptions = listOf<TransportChannel?>(
        null,
        TransportChannel.BLE,
        TransportChannel.WIFI,
        TransportChannel.MQTT_CLOUD,
    )

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
    ) {
        Text("Transport", style = MaterialTheme.typography.headlineLarge)

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text("Canal actif", style = MaterialTheme.typography.titleMedium)
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Icon(
                        imageVector = activeChannel.icon,
                        contentDescription = null,
                    )
                    Text(
                        text = activeChannel.displayName,
                        style = MaterialTheme.typography.bodyLarge,
                    )
                }
            }
        }

        Card(colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)) {
            Column(
                modifier = Modifier.padding(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Text("Forcer un canal", style = MaterialTheme.typography.titleMedium)

                SingleChoiceSegmentedButtonRow(modifier = Modifier.fillMaxWidth()) {
                    channelOptions.forEachIndexed { index, channel ->
                        SegmentedButton(
                            selected = forceChannel == channel,
                            onClick = { viewModel.forceChannel.value = channel },
                            shape = SegmentedButtonDefaults.itemShape(
                                index = index,
                                count = channelOptions.size,
                            ),
                        ) {
                            Text(
                                text = channel?.displayName ?: "Auto",
                                style = MaterialTheme.typography.labelSmall,
                            )
                        }
                    }
                }

                Text(
                    text = "Auto = BLE > WiFi > Cloud > Offline",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
