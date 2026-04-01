package com.kxkm.bmu.ui.components

import androidx.compose.material3.Icon
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import com.kxkm.bmu.shared.model.BatteryStatus
import com.kxkm.bmu.util.color
import com.kxkm.bmu.util.displayName
import com.kxkm.bmu.util.icon

@Composable
fun BatteryStateIcon(
    state: BatteryStatus,
    modifier: Modifier = Modifier,
) {
    Icon(
        imageVector = state.icon,
        contentDescription = state.displayName,
        tint = state.color,
        modifier = modifier,
    )
}
