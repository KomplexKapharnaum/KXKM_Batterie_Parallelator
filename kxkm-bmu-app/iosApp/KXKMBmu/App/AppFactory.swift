import Foundation
import Shared

/// Point d'accès unique au SharedFactory KMP et au bridge MQTT.
/// Tous les ViewModels passent par AppFactory.shared.factory.
final class AppFactory {
    static let shared = AppFactory()

    let factory: SharedFactory

    // Credentials kxkm-ai (Tailscale)
    static let mqttHost = "100.87.54.119"
    static let mqttUser = "bmu"
    static let mqttPass = "gSvfwhW4YzgzZmP2oLNbRTah5ZbKQRd"

    private init() {
        factory = SharedFactory.companion.create(driverFactory: DriverFactory())
        // Démarrer le transport cloud automatiquement
        startMqtt(host: Self.mqttHost, username: Self.mqttUser, password: Self.mqttPass)
    }

    /// Appeler après authentification, quand la config cloud est connue.
    func startMqtt(host: String, port: UInt16 = 1883,
                   username: String? = nil, password: String? = nil) {
        // Configure le transport cloud côté Kotlin
        factory.configureCloud(
            apiUrl: "http://\(host):8400",
            apiKey: "",
            mqttBroker: "mqtt://\(host):\(port)",
            mqttUser: username ?? "",
            mqttPass: password ?? ""
        )
        // Démarre le client MQTT natif iOS et le relie au SharedFactory
        MqttBridge.shared.start(
            factory: factory,
            host: host, port: port,
            username: username, password: password
        )
    }

    func stopMqtt() {
        MqttBridge.shared.stop()
    }
}
