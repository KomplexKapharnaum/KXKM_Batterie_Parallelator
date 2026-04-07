import Foundation
import Shared

/// Pont entre MqttClientIOS (Swift/Network) et MqttTransport (Kotlin/Shared).
/// Initialise la connexion MQTT et route les messages vers le SharedFactory.
final class MqttBridge {
    static let shared = MqttBridge()

    private var client: MqttClientIOS?
    private var factory: SharedFactory?

    private init() {}

    /// Appeler après configureCloud() sur le SharedFactory.
    func start(factory: SharedFactory, host: String, port: UInt16 = 1883,
               username: String? = nil, password: String? = nil) {
        self.factory = factory
        let mqtt = MqttClientIOS(
            host: host, port: port,
            username: username, password: password,
            topic: "bmu/+/battery/#"
        )
        mqtt.onMessage = { [weak self] topic, payload in
            self?.factory?.onCloudMqttMessage(topic: topic, payload: payload)
        }
        mqtt.onConnectionChange = { [weak self] connected in
            self?.factory?.setMqttConnected(connected: connected)
        }
        self.client = mqtt
        mqtt.connect()
    }

    func stop() {
        client?.disconnect()
        client = nil
    }
}
