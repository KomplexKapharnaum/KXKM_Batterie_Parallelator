package com.kxkm.bmu.ui.soh

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
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
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

@Composable
fun SohGauge(
    sohPercent: Int,
    modifier: Modifier = Modifier,
    size: Dp = 72.dp,
    strokeWidth: Dp = 6.dp,
) {
    val color = when {
        sohPercent >= 80 -> Color(0xFF4CAF50) // green
        sohPercent >= 60 -> Color(0xFFFF9800) // orange
        else -> Color(0xFFF44336)              // red
    }

    Box(modifier = modifier.size(size), contentAlignment = Alignment.Center) {
        Canvas(modifier = Modifier.size(size)) {
            val stroke = Stroke(width = strokeWidth.toPx(), cap = StrokeCap.Round)
            val arcSize = Size(this.size.width - stroke.width, this.size.height - stroke.width)
            val topLeft = Offset(stroke.width / 2f, stroke.width / 2f)

            // Background arc (full 270 degrees)
            drawArc(
                color = color.copy(alpha = 0.15f),
                startAngle = 135f,
                sweepAngle = 270f,
                useCenter = false,
                topLeft = topLeft,
                size = arcSize,
                style = stroke,
            )

            // Foreground arc (proportional to SOH)
            val sweep = 270f * (sohPercent.coerceIn(0, 100) / 100f)
            drawArc(
                color = color,
                startAngle = 135f,
                sweepAngle = sweep,
                useCenter = false,
                topLeft = topLeft,
                size = arcSize,
                style = stroke,
            )
        }

        Text(
            text = "$sohPercent%",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            fontFamily = FontFamily.Monospace,
            fontSize = if (size > 64.dp) 16.sp else 12.sp,
            color = color,
        )
    }
}
