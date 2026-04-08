import SwiftUI

class ConfigViewModel: ObservableObject {
    // Protection
    @Published var minMv: Int = 24000
    @Published var maxMv: Int = 30000
    @Published var maxMa: Int = 10000
    @Published var diffMv: Int = 1000

    // WiFi
    @Published var wifiSsid: String = ""
    @Published var wifiPassword: String = ""
    @Published var wifiStatus: WifiStatusInfo? = nil

    // MQTT
    @Published var mqttUri: String = ""
    @Published var mqttUsername: String = ""
    @Published var mqttPassword: String = ""

    // Device name
    @Published var deviceName: String = ""

    // Users
    @Published var users: [UserProfile] = []

    // Sync
    @Published var syncUrl: String = ""
    @Published var mqttBroker: String = ""
    @Published var syncPending: Int = 0
    @Published var lastSyncTime: Date? = nil

    // Transport
    @Published var activeChannel: TransportChannel = .offline
    @Published var forceChannel: TransportChannel? = nil

    @Published var statusMessage: String? = nil

    private let ble = BleManager.shared
    private let authUseCase = AuthUseCase()

    init() {
        activeChannel = ble.isConnected ? .ble : .offline
        users = authUseCase.getAllUsers()
    }

    func loadAll() {
        users = authUseCase.getAllUsers()
        mqttUri = ble.mqttUri ?? ""
        mqttUsername = ble.mqttUsername ?? ""
        deviceName = ble.connectedDeviceName ?? ""
    }

    func saveProtection() {
        let config = ProtectionConfig(minMv: minMv, maxMv: maxMv, maxMa: maxMa, diffMv: diffMv)
        ble.setProtectionConfig(config)
        statusMessage = "Envoyé via BLE"
    }

    func sendWifiConfig() {
        ble.setWifiConfig(ssid: wifiSsid, password: wifiPassword)
        statusMessage = ble.isConnected ? "WiFi configuré" : "BLE non connecté"
    }

    func sendMqttConfig() {
        ble.setMqttConfig(uri: mqttUri, username: mqttUsername, password: mqttPassword)
        statusMessage = "MQTT config envoyee"
    }

    func sendDeviceName() {
        ble.setDeviceName(deviceName)
        statusMessage = "Nom appareil mis a jour"
    }

    func deleteUser(_ user: UserProfile) {
        authUseCase.deleteUser(userId: user.id)
        users = authUseCase.getAllUsers()
    }

    func createUser(name: String, pin: String, role: UserRole) {
        authUseCase.createUser(name: name, pin: pin, role: role)
        users = authUseCase.getAllUsers()
    }
}
