import SwiftUI
import NetworkExtension
import CoreLocation

@MainActor
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
    @Published var iPhoneSSID: String? = nil

    private let ble = BleManager.shared
    private let authUseCase = AuthUseCase()
    private let locationManager = CLLocationManager()
    private var observeTasks: [Task<Void, Never>] = []

    init() {
        activeChannel = ble.isConnected ? .ble : .offline
        users = authUseCase.getAllUsers()
        startObserving()
    }

    deinit {
        for task in observeTasks { task.cancel() }
    }

    private func startObserving() {
        // Observe WiFi status from BMU
        let wifiTask = Task { [weak self] in
            guard let self else { return }
            for await status in BleManager.shared.$wifiStatus.values {
                guard let status else { continue }
                self.wifiStatus = status
                // Pre-fill SSID from BMU if field is empty
                if self.wifiSsid.isEmpty && !status.ssid.isEmpty {
                    self.wifiSsid = status.ssid
                    self.tryLoadKeychainPassword(for: status.ssid)
                }
            }
        }
        observeTasks.append(wifiTask)
    }

    /// Try to load a saved password from Keychain for the given SSID.
    private func tryLoadKeychainPassword(for ssid: String) {
        if let saved = WifiKeychain.load(ssid: ssid) {
            wifiPassword = saved
            statusMessage = "Mot de passe récupéré (Keychain)"
        }
    }

    func loadAll() {
        users = authUseCase.getAllUsers()
        mqttUri = ble.mqttUri ?? ""
        mqttUsername = ble.mqttUsername ?? ""
        deviceName = ble.connectedDeviceName ?? ""
        // Load WiFi status from BMU
        if let status = ble.wifiStatus {
            wifiStatus = status
            if wifiSsid.isEmpty && !status.ssid.isEmpty {
                wifiSsid = status.ssid
                tryLoadKeychainPassword(for: status.ssid)
            }
        }
    }

    func saveProtection() {
        let config = ProtectionConfig(minMv: minMv, maxMv: maxMv, maxMa: maxMa, diffMv: diffMv)
        ble.setProtectionConfig(config)
        statusMessage = "Envoyé via BLE"
    }

    func sendWifiConfig() {
        ble.setWifiConfig(ssid: wifiSsid, password: wifiPassword)
        if ble.isConnected {
            // Save password to Keychain for future auto-fill
            WifiKeychain.save(ssid: wifiSsid, password: wifiPassword)
            statusMessage = "WiFi configuré"
        } else {
            statusMessage = "BLE non connecté"
        }
    }

    func sendMqttConfig() {
        ble.setMqttConfig(uri: mqttUri, username: mqttUsername, password: mqttPassword)
        statusMessage = "MQTT config envoyee"
    }

    func sendDeviceName() {
        ble.setDeviceName(deviceName)
        statusMessage = "Nom appareil mis a jour"
    }

    /// Fetch the SSID of the WiFi network the iPhone is currently connected to.
    /// Requires: Access WiFi Information entitlement + Location When In Use permission.
    func fetchCurrentSSID() {
        // Request location permission if needed (required to read SSID on iOS 13+)
        let status = locationManager.authorizationStatus
        if status == .notDetermined {
            locationManager.requestWhenInUseAuthorization()
            // Will need to call again after user responds
            statusMessage = "Autorisez la localisation puis réessayez"
            return
        }
        if status == .denied || status == .restricted {
            statusMessage = "Localisation requise pour lire le SSID (Réglages > KXKM BMU)"
            return
        }

        NEHotspotNetwork.fetchCurrent { [weak self] network in
            Task { @MainActor in
                guard let self else { return }
                if let ssid = network?.ssid, !ssid.isEmpty {
                    self.wifiSsid = ssid
                    self.iPhoneSSID = ssid
                    self.tryLoadKeychainPassword(for: ssid)
                    if self.wifiPassword.isEmpty {
                        self.statusMessage = "SSID '\(ssid)' récupéré"
                    } else {
                        self.statusMessage = "SSID '\(ssid)' + mot de passe récupérés"
                    }
                } else {
                    self.statusMessage = "Impossible de lire le SSID — vérifiez que le WiFi est activé"
                }
            }
        }
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
