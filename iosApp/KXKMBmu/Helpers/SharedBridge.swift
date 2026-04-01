// import Shared — using Stubs
import SwiftUI

// MARK: - BatteryStatus convenience

extension BatteryStatus {
    var displayName: String {
        switch self {
        case .connected: return "Connecté"
        case .disconnected: return "Déconnecté"
        case .reconnecting: return "Reconnexion"
        case .error: return "Erreur"
        case .locked: return "Verrouillé"
        default: return "Inconnu"
        }
    }

    var color: Color {
        switch self {
        case .connected: return .green
        case .disconnected: return .red
        case .reconnecting: return .yellow
        case .error: return .orange
        case .locked: return .red
        default: return .gray
        }
    }

    var icon: String {
        switch self {
        case .connected: return "bolt.fill"
        case .disconnected: return "bolt.slash"
        case .reconnecting: return "arrow.clockwise"
        case .error: return "exclamationmark.triangle"
        case .locked: return "lock.fill"
        default: return "questionmark"
        }
    }
}

// MARK: - UserRole convenience

extension UserRole {
    var displayName: String {
        switch self {
        case .admin: return "Admin"
        case .technician: return "Technicien"
        case .viewer: return "Lecteur"
        default: return "Inconnu"
        }
    }

    var canControl: Bool {
        self == .admin || self == .technician
    }

    var canConfigure: Bool {
        self == .admin
    }
}

// MARK: - Transport channel display

extension TransportChannel {
    var displayName: String {
        switch self {
        case .ble: return "BLE"
        case .wifi: return "WiFi"
        case .mqttCloud: return "Cloud MQTT"
        case .restCloud: return "Cloud REST"
        case .offline: return "Hors ligne"
        default: return "—"
        }
    }

    var icon: String {
        switch self {
        case .ble: return "antenna.radiowaves.left.and.right"
        case .wifi: return "wifi"
        case .mqttCloud, .restCloud: return "cloud"
        case .offline: return "icloud.slash"
        default: return "questionmark"
        }
    }
}

// MARK: - Voltage formatting

extension Int32 {
    /// Convert mV to display string "XX.XX V"
    var voltageDisplay: String {
        String(format: "%.2f V", Double(self) / 1000.0)
    }

    /// Convert mA to display string "X.XX A"
    var currentDisplay: String {
        String(format: "%.2f A", Double(self) / 1000.0)
    }

    /// Convert mAh to display string "X.XX Ah"
    var ahDisplay: String {
        String(format: "%.2f Ah", Double(self) / 1000.0)
    }
}
