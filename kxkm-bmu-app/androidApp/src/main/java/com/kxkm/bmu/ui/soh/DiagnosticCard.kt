package com.kxkm.bmu.ui.soh

import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.kxkm.bmu.shared.model.Diagnostic
import com.kxkm.bmu.shared.model.DiagnosticSeverity
import java.text.SimpleDateFormat
import java.util.*

@Composable
fun DiagnosticCard(
    diagnostic: Diagnostic?,
    isLoading: Boolean,
    onRefresh: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val (bgColor, iconColor, icon) = when (diagnostic?.severity) {
        DiagnosticSeverity.critical -> Triple(
            Color(0xFFFDE8E8), Color(0xFFF44336), Icons.Filled.Warning
        )
        DiagnosticSeverity.warning -> Triple(
            Color(0xFFFFF3E0), Color(0xFFFF9800), Icons.Filled.Warning
        )
        else -> Triple(
            Color(0xFFE8F5E9), Color(0xFF4CAF50), Icons.Filled.Info
        )
    }

    Card(
        modifier = modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = bgColor),
    ) {
        Column(modifier = Modifier.padding(16.dp)) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    Icon(icon, contentDescription = null, tint = iconColor)
                    Text(
                        text = "Diagnostic IA",
                        style = MaterialTheme.typography.titleSmall,
                    )
                }
                IconButton(onClick = onRefresh, enabled = !isLoading) {
                    if (isLoading) {
                        CircularProgressIndicator(modifier = Modifier.size(20.dp), strokeWidth = 2.dp)
                    } else {
                        Icon(Icons.Filled.Refresh, contentDescription = "G\u00e9n\u00e9rer diagnostic")
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            if (diagnostic != null) {
                Text(
                    text = diagnostic.diagnostic,
                    style = MaterialTheme.typography.bodyMedium,
                )
                Spacer(modifier = Modifier.height(4.dp))
                val fmt = SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.FRANCE)
                Text(
                    text = "G\u00e9n\u00e9r\u00e9 le ${fmt.format(Date(diagnostic.generatedAt * 1000))}",
                    style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            } else {
                Text(
                    text = "Aucun diagnostic disponible. Appuyez sur rafra\u00eechir pour en g\u00e9n\u00e9rer un.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}
