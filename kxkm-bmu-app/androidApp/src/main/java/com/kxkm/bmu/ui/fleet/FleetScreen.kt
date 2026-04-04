package com.kxkm.bmu.ui.fleet

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.viewmodel.FleetViewModel

@Composable
fun FleetScreen(
    onBatteryClick: (Int) -> Unit = {},
    viewModel: FleetViewModel = hiltViewModel(),
) {
    val fleetHealth by viewModel.fleetHealth.collectAsState()
    val scores by viewModel.scores.collectAsState()

    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text("Vue Flotte", style = MaterialTheme.typography.headlineSmall)
            IconButton(onClick = { viewModel.refresh() }) {
                Icon(Icons.Filled.Refresh, contentDescription = "Rafra\u00eechir")
            }
        }

        Spacer(modifier = Modifier.height(24.dp))

        fleetHealth?.let { fleet ->
            FleetHealthCircle(
                healthPercent = (fleet.fleetHealth * 100).toInt(),
            )

            Spacer(modifier = Modifier.height(16.dp))

            // Imbalance indicator
            Text(
                text = "D\u00e9s\u00e9quilibre: ${String.format("%.0f%%", fleet.imbalanceSeverity * 100)}",
                style = MaterialTheme.typography.bodyMedium,
                color = when {
                    fleet.imbalanceSeverity > 0.5f -> MaterialTheme.colorScheme.error
                    fleet.imbalanceSeverity > 0.2f -> MaterialTheme.colorScheme.tertiary
                    else -> MaterialTheme.colorScheme.onSurfaceVariant
                },
            )

            Spacer(modifier = Modifier.height(16.dp))

            // Outlier card
            val outlierScore = scores.firstOrNull { it.battery == fleet.outlierIdx }
            if (fleet.outlierScore > 0.3f && outlierScore != null) {
                Card(
                    onClick = { onBatteryClick(fleet.outlierIdx) },
                    colors = CardDefaults.cardColors(
                        containerColor = when {
                            fleet.outlierScore > 0.7f -> Color(0xFFFDE8E8)
                            else -> Color(0xFFFFF3E0)
                        },
                    ),
                    modifier = Modifier.fillMaxWidth(),
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        Icon(
                            Icons.Filled.Warning,
                            contentDescription = "Attention",
                            tint = if (fleet.outlierScore > 0.7f) Color(0xFFF44336) else Color(0xFFFF9800),
                        )
                        Column {
                            Text(
                                text = "Batterie ${fleet.outlierIdx + 1} \u2014 anomalie d\u00e9tect\u00e9e",
                                fontWeight = FontWeight.Bold,
                                style = MaterialTheme.typography.bodyLarge,
                            )
                            Text(
                                text = "Score anomalie: ${String.format("%.0f%%", fleet.outlierScore * 100)} | SOH: ${String.format("%.0f%%", outlierScore.sohScore * 100)}",
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                            Text(
                                text = "Appuyez pour voir le d\u00e9tail",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.primary,
                            )
                        }
                    }
                }
            }
        } ?: run {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = "Donn\u00e9es flotte non disponibles",
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
