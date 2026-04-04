package com.kxkm.bmu.ui.fleet

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.*
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun FleetHealthCircle(
    healthPercent: Int,
    modifier: Modifier = Modifier,
) {
    val color = when {
        healthPercent >= 80 -> Color(0xFF4CAF50)
        healthPercent >= 60 -> Color(0xFFFF9800)
        else -> Color(0xFFF44336)
    }

    Box(modifier = modifier.size(160.dp), contentAlignment = Alignment.Center) {
        Canvas(modifier = Modifier.size(160.dp)) {
            val stroke = Stroke(width = 12.dp.toPx(), cap = StrokeCap.Round)
            val arcSize = Size(size.width - stroke.width, size.height - stroke.width)
            val topLeft = Offset(stroke.width / 2f, stroke.width / 2f)

            drawArc(
                color = color.copy(alpha = 0.12f),
                startAngle = 135f, sweepAngle = 270f,
                useCenter = false, topLeft = topLeft, size = arcSize, style = stroke,
            )
            drawArc(
                color = color,
                startAngle = 135f,
                sweepAngle = 270f * (healthPercent.coerceIn(0, 100) / 100f),
                useCenter = false, topLeft = topLeft, size = arcSize, style = stroke,
            )
        }

        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            Text(
                text = "$healthPercent%",
                fontSize = 32.sp,
                fontWeight = FontWeight.Bold,
                fontFamily = FontFamily.Monospace,
                color = color,
            )
            Text(
                text = "Sant\u00e9 flotte",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}
