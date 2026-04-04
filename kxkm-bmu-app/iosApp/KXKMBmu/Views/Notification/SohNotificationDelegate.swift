import UserNotifications
import Shared

class SohNotificationDelegate {
    private var notifiedBatteries: Set<Int> = []

    func requestPermission() {
        UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound, .badge]) { _, _ in }
    }

    func checkAndNotify(scores: [MlScore]) {
        let alerts = scores.filter { $0.anomalyScore > 0.7 || $0.sohScore < 0.7 }

        for score in alerts {
            guard !notifiedBatteries.contains(Int(score.battery)) else { continue }
            notifiedBatteries.insert(Int(score.battery))

            let sohPct = Int(score.sohScore * 100)
            let content = UNMutableNotificationContent()
            content.title = "Batterie \(score.battery + 1) \u2014 Alerte"
            if score.anomalyScore > 0.7 {
                content.body = String(format: "Anomalie d\u00e9tect\u00e9e: %.0f%%", score.anomalyScore * 100)
            } else {
                content.body = "SOH critique: \(sohPct)%. RUL estim\u00e9: \(score.rulDays) jours."
            }
            content.sound = .default

            let request = UNNotificationRequest(
                identifier: "soh_alert_\(score.battery)",
                content: content,
                trigger: nil
            )
            UNUserNotificationCenter.current().add(request)
        }

        let alertBatteries = Set(alerts.map { Int($0.battery) })
        notifiedBatteries = notifiedBatteries.intersection(alertBatteries)
    }
}
