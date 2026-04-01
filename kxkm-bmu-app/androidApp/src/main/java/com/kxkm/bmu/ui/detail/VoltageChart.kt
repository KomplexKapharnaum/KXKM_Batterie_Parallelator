package com.kxkm.bmu.ui.detail

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.kxkm.bmu.shared.model.BatteryHistoryPoint
import com.patrykandpatrick.vico.compose.cartesian.CartesianChartHost
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberBottomAxis
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberStartAxis
import com.patrykandpatrick.vico.compose.cartesian.layer.rememberLineCartesianLayer
import com.patrykandpatrick.vico.compose.cartesian.rememberCartesianChart
import com.patrykandpatrick.vico.core.cartesian.data.CartesianChartModelProducer
import com.patrykandpatrick.vico.core.cartesian.data.lineSeries
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

@Composable
fun VoltageChart(
    history: List<BatteryHistoryPoint>,
    modifier: Modifier = Modifier,
) {
    if (history.isEmpty()) {
        Text(
            text = "Pas d'historique disponible",
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.bodyMedium,
        )
        return
    }

    val modelProducer = remember { CartesianChartModelProducer() }

    // Update chart data when history changes
    remember(history) {
        modelProducer.runTransaction {
            lineSeries {
                series(
                    history.map { it.voltageMv / 1000.0 }
                )
            }
        }
    }

    CartesianChartHost(
        chart = rememberCartesianChart(
            rememberLineCartesianLayer(),
            startAxis = rememberStartAxis(
                title = "Tension (V)",
            ),
            bottomAxis = rememberBottomAxis(
                valueFormatter = { value, _, _ ->
                    val index = value.toInt().coerceIn(0, history.size - 1)
                    val ts = history.getOrNull(index)?.timestamp ?: 0L
                    val fmt = SimpleDateFormat("HH:mm", Locale.FRANCE)
                    fmt.format(Date(ts))
                },
            ),
        ),
        modelProducer = modelProducer,
        modifier = modifier,
    )
}
