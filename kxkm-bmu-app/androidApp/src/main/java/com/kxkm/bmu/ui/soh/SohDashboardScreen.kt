package com.kxkm.bmu.ui.soh

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.shared.model.MlScore
import com.kxkm.bmu.viewmodel.SohDashboardViewModel
import java.text.SimpleDateFormat
import java.util.*

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SohDashboardScreen(
    onBatteryClick: (Int) -> Unit = {},
    viewModel: SohDashboardViewModel = hiltViewModel(),
) {
    val scores by viewModel.scores.collectAsState()
    val isRefreshing by viewModel.isRefreshing.collectAsState()
    val lastRefreshMs by viewModel.lastRefreshMs.collectAsState()
    val sohHistory by viewModel.sohHistory.collectAsState()

    // Load history for each battery on first appearance
    LaunchedEffect(scores) {
        scores.forEach { viewModel.loadHistory(it.battery) }
    }

    Column(modifier = Modifier.fillMaxSize().padding(8.dp)) {
        // Header with refresh
        Row(
            modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column {
                Text(
                    text = "SOH Batteries",
                    style = MaterialTheme.typography.headlineSmall,
                )
                if (lastRefreshMs > 0) {
                    val fmt = SimpleDateFormat("HH:mm dd/MM", Locale.FRANCE)
                    Text(
                        text = "Mis \u00e0 jour: ${fmt.format(Date(lastRefreshMs))}",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            IconButton(onClick = { viewModel.refresh() }, enabled = !isRefreshing) {
                if (isRefreshing) {
                    CircularProgressIndicator(modifier = Modifier.size(24.dp), strokeWidth = 2.dp)
                } else {
                    Icon(Icons.Filled.Refresh, contentDescription = "Rafra\u00eechir")
                }
            }
        }

        if (scores.isEmpty()) {
            Box(
                modifier = Modifier.fillMaxSize(),
                contentAlignment = Alignment.Center,
            ) {
                Text(
                    text = "Aucune donn\u00e9e SOH disponible.\nV\u00e9rifiez la connexion au serveur.",
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        } else {
            LazyVerticalGrid(
                columns = GridCells.Fixed(2),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                items(scores, key = { it.battery }) { score ->
                    SohBatteryCard(
                        score = score,
                        sparklineValues = sohHistory[score.battery]
                            ?.map { it.sohScore * 100f } ?: emptyList(),
                        onClick = { onBatteryClick(score.battery) },
                    )
                }
            }
        }
    }
}

@Composable
private fun SohBatteryCard(
    score: MlScore,
    sparklineValues: List<Float>,
    onClick: () -> Unit,
) {
    val sohPct = (score.sohScore * 100).toInt()
    val anomalyColor = when {
        score.anomalyScore > 0.7f -> MaterialTheme.colorScheme.error
        score.anomalyScore > 0.3f -> MaterialTheme.colorScheme.tertiary
        else -> MaterialTheme.colorScheme.onSurfaceVariant
    }

    Card(
        onClick = onClick,
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = "Bat ${score.battery + 1}",
                    style = MaterialTheme.typography.labelMedium,
                )
                SohGauge(sohPercent = sohPct, size = 48.dp, strokeWidth = 4.dp)
            }

            // RUL
            Text(
                text = "RUL: ${score.rulDays}j",
                style = MaterialTheme.typography.bodySmall,
                fontFamily = FontFamily.Monospace,
            )

            // Anomaly score
            Text(
                text = "Anomalie: ${String.format("%.0f%%", score.anomalyScore * 100)}",
                style = MaterialTheme.typography.bodySmall,
                color = anomalyColor,
            )

            // R_int trend
            if (score.rIntTrendMohmPerDay != 0f) {
                Text(
                    text = "R_int: ${String.format("%+.2f", score.rIntTrendMohmPerDay)} m\u03a9/j",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            // Sparkline
            if (sparklineValues.size >= 2) {
                SohSparkline(
                    values = sparklineValues,
                    color = when {
                        sohPct >= 80 -> androidx.compose.ui.graphics.Color(0xFF4CAF50)
                        sohPct >= 60 -> androidx.compose.ui.graphics.Color(0xFFFF9800)
                        else -> androidx.compose.ui.graphics.Color(0xFFF44336)
                    },
                )
            }
        }
    }
}
