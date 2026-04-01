import SwiftUI
import Shared

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

    private let configUseCase: ConfigUseCase
    private let authUseCase: AuthUseCase

    init() {
        self.configUseCase = SharedFactory.companion.createConfigUseCase()
        self.authUseCase = SharedFactory.companion.createAuthUseCase()
        loadAll()
    }

    func loadAll() {
        let cfg = configUseCase.getCurrentConfig()
        minMv = Int(cfg.minMv)
        maxMv = Int(cfg.maxMv)
        maxMa = Int(cfg.maxMa)
        diffMv = Int(cfg.diffMv)

        users = authUseCase.getAllUsers()
        syncPending = Int(configUseCase.getPendingSyncCount())
    }

    func saveProtection() {
        configUseCase.setProtectionConfig(
            minMv: Int32(minMv), maxMv: Int32(maxMv),
            maxMa: Int32(maxMa), diffMv: Int32(diffMv)
        ) { [weak self] result in
            DispatchQueue.main.async {
                self?.statusMessage = result.isSuccess ? "Seuils mis à jour" : "Erreur"
            }
        }
    }

    func sendWifiConfig() {
        configUseCase.setWifiConfig(ssid: wifiSsid, password: wifiPassword) { [weak self] result in
            DispatchQueue.main.async {
                self?.statusMessage = result.isSuccess ? "WiFi configuré" : "Erreur (BLE requis)"
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
