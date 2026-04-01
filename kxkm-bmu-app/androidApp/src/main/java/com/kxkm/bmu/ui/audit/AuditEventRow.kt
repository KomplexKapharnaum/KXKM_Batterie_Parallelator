package com.kxkm.bmu.ui.audit

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bolt
import androidx.compose.material.icons.filled.BoltOutlined
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.unit.dp
import com.kxkm.bmu.shared.model.AuditEvent
import com.kxkm.bmu.ui.theme.KxkmBlue
import com.kxkm.bmu.ui.theme.KxkmGreen
import com.kxkm.bmu.ui.theme.KxkmRed
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun AuditEventRow(
    event: AuditEvent,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(vertical = 8.dp, horizontal = 16.dp),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.Top,
    ) {
        Icon(
            imageVector = event.actionIcon,
            contentDescription = null,
            tint = event.actionColor,
            modifier = Modifier.size(24.dp),
        )

        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = event.action
                    .replace("_", " ")
                    .replaceFirstChar { it.uppercase() },
                style = MaterialTheme.typography.bodyMedium,
            )
            event.target?.let { target ->
                Text(
                    text = "Batterie ${target + 1}",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            event.detail?.let { detail ->
                Text(
                    text = detail,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }

        Column(horizontalAlignment = Alignment.End) {
            Text(
                text = event.userId,
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = formatTimestamp(event.timestamp),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

private val AuditEvent.actionIcon: ImageVector
    get() = when (action) {
        "switch_on" -> Icons.Filled.Bolt
        "switch_off" -> Icons.Filled.BoltOutlined
        "reset" -> Icons.Filled.Refresh
        "config_change" -> Icons.Filled.Settings
        "wifi_config" -> Icons.Filled.Wifi
        else -> Icons.Filled.Description
    }

private val AuditEvent.actionColor: Color
    get() = when (action) {
        "switch_on" -> KxkmGreen
        "switch_off" -> KxkmRed
        "config_change", "wifi_config" -> KxkmBlue
        else -> Color.Gray
    }

private fun formatTimestamp(ms: Long): String {
    val fmt = SimpleDateFormat("dd/MM HH:mm:ss", Locale.FRANCE)
    return fmt.format(Date(ms))
}
