package com.kxkm.bmu.ui.soh

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import com.kxkm.bmu.shared.model.MlScore
import com.patrykandpatrick.vico.compose.cartesian.CartesianChartHost
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberBottomAxis
import com.patrykandpatrick.vico.compose.cartesian.axis.rememberStartAxis
import com.patrykandpatrick.vico.compose.cartesian.layer.rememberLineCartesianLayer
import com.patrykandpatrick.vico.compose.cartesian.rememberCartesianChart
import com.patrykandpatrick.vico.core.cartesian.data.CartesianChartModelProducer
import com.patrykandpatrick.vico.core.cartesian.data.lineSeries
import java.text.SimpleDateFormat
import java.util.*

@Composable
fun RintTrendChart(
    history: List<MlScore>,
    modifier: Modifier = Modifier,
) {
    if (history.size < 2) {
        Text(
            text = "Pas assez de donn\u00e9es pour le graphique R_int",
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            style = MaterialTheme.typography.bodyMedium,
        )
        return
    }

    val modelProducer = remember { CartesianChartModelProducer() }

    remember(history) {
        modelProducer.runTransaction {
            lineSeries {
                // R_int trend = cumulative from rIntTrendMohmPerDay
                series(history.map { it.rIntTrendMohmPerDay.toDouble() })
            }
        }
    }

    CartesianChartHost(
        chart = rememberCartesianChart(
            rememberLineCartesianLayer(),
            startAxis = rememberStartAxis(
                title = "R_int trend (m\u03a9/j)",
            ),
            bottomAxis = rememberBottomAxis(
                valueFormatter = { value, _, _ ->
                    val index = value.toInt().coerceIn(0, history.size - 1)
                    val ts = history.getOrNull(index)?.timestamp ?: 0L
                    val fmt = SimpleDateFormat("dd/MM", Locale.FRANCE)
                    fmt.format(Date(ts * 1000))
                },
            ),
        ),
        modelProducer = modelProducer,
        modifier = modifier,
    )
}
