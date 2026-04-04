package com.kxkm.bmu.ui.dashboard

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.SwapVert
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.kxkm.bmu.shared.model.BatteryHealth
import com.kxkm.bmu.shared.model.BatteryState
import com.kxkm.bmu.ui.components.BatteryStateIcon
import com.kxkm.bmu.util.color
import com.kxkm.bmu.util.currentDisplay
import com.kxkm.bmu.util.voltageDisplay

@Composable
fun BatteryCellCard(
    battery: BatteryState,
    health: BatteryHealth? = null,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Card(
        onClick = onClick,
        modifier = modifier,
        border = BorderStroke(2.dp, battery.state.color.copy(alpha = 0.5f)),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
        ),
    ) {
        Column(
            modifier = Modifier.padding(12.dp),
            verticalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(
                    text = "Bat ${battery.index + 1}",
                    style = MaterialTheme.typography.labelMedium,
                )
                BatteryStateIcon(state = battery.state)
            }

            Text(
                text = battery.voltageMv.voltageDisplay(),
                style = MaterialTheme.typography.titleLarge,
                fontFamily = FontFamily.Monospace,
                color = voltageColor(battery.voltageMv),
            )

            Text(
                text = battery.currentMa.currentDisplay(),
                style = MaterialTheme.typography.bodySmall,
                fontFamily = FontFamily.Monospace,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            // SOH badge (if available)
            health?.let { h ->
                if (h.sohPercent > 0) {
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        Text(
                            text = "SOH",
                            style = MaterialTheme.typography.labelSmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        Text(
                            text = "${h.sohPercent}%",
                            style = MaterialTheme.typography.labelMedium,
                            fontFamily = FontFamily.Monospace,
                            color = sohColor(h.sohPercent),
                        )
                    }
                }
            }

            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(4.dp),
            ) {
                Icon(
                    imageVector = Icons.Filled.SwapVert,
                    contentDescription = "Switches",
                    tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(0.dp),
                )
                Text(
                    text = "${battery.nbSwitch}",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun sohColor(percent: Int) = when {
    percent >= 80 -> MaterialTheme.colorScheme.primary     // green/healthy
    percent >= 60 -> MaterialTheme.colorScheme.tertiary    // orange/warning
    else -> MaterialTheme.colorScheme.error                // red/critical
}

@Composable
private fun voltageColor(mv: Int) = when {
    mv < 24000 || mv > 30000 -> MaterialTheme.colorScheme.error
    mv < 24500 || mv > 29500 -> MaterialTheme.colorScheme.tertiary
    else -> MaterialTheme.colorScheme.onSurface
}
