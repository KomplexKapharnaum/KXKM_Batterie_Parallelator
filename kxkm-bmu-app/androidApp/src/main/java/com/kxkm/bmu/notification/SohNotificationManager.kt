package com.kxkm.bmu.notification

import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.os.Build
import androidx.core.app.NotificationCompat
import com.kxkm.bmu.R
import com.kxkm.bmu.shared.domain.SohUseCase
import kotlinx.coroutines.*

/**
 * Monitors cached ML scores and fires local notifications when
 * anomaly_score > 0.7 or SOH < 70%.
 */
class SohNotificationManager(
    private val context: Context,
    private val sohUseCase: SohUseCase,
) {
    private val scope = CoroutineScope(Dispatchers.Default + SupervisorJob())
    private val notifiedBatteries = mutableSetOf<Int>()

    companion object {
        private const val CHANNEL_ID = "soh_alerts"
        private const val CHANNEL_NAME = "Alertes SOH"
        private const val NOTIFICATION_BASE_ID = 9000
        private const val CHECK_INTERVAL_MS = 5 * 60 * 1000L // 5 min
    }

    fun start() {
        createChannel()
        scope.launch {
            while (true) {
                checkAndNotify()
                delay(CHECK_INTERVAL_MS)
            }
        }
    }

    fun stop() {
        scope.cancel()
    }

    private fun createChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID, CHANNEL_NAME, NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "Alertes de sant\u00e9 batterie (SOH, anomalies)"
            }
            val nm = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            nm.createNotificationChannel(channel)
        }
    }

    private fun checkAndNotify() {
        val alerts = sohUseCase.getAnomalyAlerts()
        val nm = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

        alerts.forEach { score ->
            // Only notify once per battery until score returns to normal
            if (score.battery in notifiedBatteries) return@forEach
            notifiedBatteries.add(score.battery)

            val sohPct = (score.sohScore * 100).toInt()
            val title = "Batterie ${score.battery + 1} \u2014 Alerte"
            val text = when {
                score.anomalyScore > 0.7f && sohPct < 70 ->
                    "Anomalie d\u00e9tect\u00e9e (${String.format("%.0f%%", score.anomalyScore * 100)}) et SOH faible ($sohPct%)"
                score.anomalyScore > 0.7f ->
                    "Anomalie d\u00e9tect\u00e9e: score ${String.format("%.0f%%", score.anomalyScore * 100)}"
                else ->
                    "SOH critique: $sohPct%. RUL estim\u00e9: ${score.rulDays} jours."
            }

            val notification = NotificationCompat.Builder(context, CHANNEL_ID)
                .setSmallIcon(R.drawable.ic_launcher_foreground)
                .setContentTitle(title)
                .setContentText(text)
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setAutoCancel(true)
                .build()

            nm.notify(NOTIFICATION_BASE_ID + score.battery, notification)
        }

        // Clear notified set for batteries that returned to normal
        val alertBatteries = alerts.map { it.battery }.toSet()
        notifiedBatteries.removeAll { it !in alertBatteries }
    }
}
