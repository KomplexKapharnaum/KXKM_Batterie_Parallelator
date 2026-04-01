package com.kxkm.bmu.ui.audit

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CloudUpload
import androidx.compose.material3.FilterChip
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.hilt.navigation.compose.hiltViewModel
import com.kxkm.bmu.viewmodel.AuditViewModel

@Composable
fun AuditScreen(
    viewModel: AuditViewModel = hiltViewModel(),
) {
    val events by viewModel.events.collectAsState()
    val filterAction by viewModel.filterAction.collectAsState()
    val pendingSync by viewModel.pendingSyncCount.collectAsState()

    LaunchedEffect(Unit) { viewModel.reload() }

    Column(modifier = Modifier.fillMaxSize()) {
        Text(
            text = "Audit",
            style = MaterialTheme.typography.headlineLarge,
            modifier = Modifier.padding(16.dp),
        )

        // Sync indicator
        if (pendingSync > 0) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Icon(
                    imageVector = Icons.Filled.CloudUpload,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.tertiary,
                )
                Text(
                    text = "$pendingSync \u00e9v\u00e9nements en attente de sync",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.tertiary,
                )
            }
        }

        // Filter chips
        LazyRow(
            contentPadding = PaddingValues(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            item {
                FilterChip(
                    selected = filterAction == null,
                    onClick = { viewModel.clearFilters() },
                    label = { Text("Tous") },
                )
            }
            item {
                FilterChip(
                    selected = filterAction == "switch",
                    onClick = { viewModel.setFilterAction("switch") },
                    label = { Text("Switch") },
                )
            }
            item {
                FilterChip(
                    selected = filterAction == "config_change",
                    onClick = { viewModel.setFilterAction("config_change") },
                    label = { Text("Config") },
                )
            }
            item {
                FilterChip(
                    selected = filterAction == "reset",
                    onClick = { viewModel.setFilterAction("reset") },
                    label = { Text("Reset") },
                )
            }
        }

        HorizontalDivider(modifier = Modifier.padding(top = 8.dp))

        // Event list
        LazyColumn(modifier = Modifier.fillMaxSize()) {
            items(events, key = { it.timestamp }) { event ->
                AuditEventRow(event = event)
                HorizontalDivider()
            }
        }
    }
}
