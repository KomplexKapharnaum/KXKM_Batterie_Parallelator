package com.kxkm.bmu.ui.soh

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.unit.dp

@Composable
fun SohSparkline(
    values: List<Float>,
    modifier: Modifier = Modifier,
    color: Color = Color(0xFF4CAF50),
) {
    if (values.size < 2) return

    Canvas(modifier = modifier.width(80.dp).height(24.dp)) {
        val maxVal = values.max().coerceAtLeast(1f)
        val minVal = values.min().coerceAtLeast(0f)
        val range = (maxVal - minVal).coerceAtLeast(0.01f)
        val stepX = size.width / (values.size - 1).toFloat()
        val padding = 2f

        val path = Path().apply {
            values.forEachIndexed { i, v ->
                val x = i * stepX
                val y = size.height - padding - ((v - minVal) / range) * (size.height - 2 * padding)
                if (i == 0) moveTo(x, y) else lineTo(x, y)
            }
        }

        drawPath(path, color, style = Stroke(width = 2.dp.toPx()))
    }
}
