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
        }
    }

    var color: Color {
        switch self {
        case .connected: return .green
        case .disconnected: return .red
        case .reconnecting: return .yellow
        case .error: return .orange
        case .locked: return .red
        }
    }

    var icon: String {
        switch self {
        case .connected: return "bolt.fill"
        case .disconnected: return "bolt.slash"
        case .reconnecting: return "arrow.clockwise"
        case .error: return "exclamationmark.triangle"
        case .locked: return "lock.fill"
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
        }
    }

    var icon: String {
        switch self {
        case .ble: return "antenna.radiowaves.left.and.right"
        case .wifi: return "wifi"
        case .mqttCloud, .restCloud: return "cloud"
        case .offline: return "icloud.slash"
        }
    }
}

// MARK: - Solar charge state (VE.Direct MPPT)

extension SolarData {
    /// VE.Direct CS (Charge State) field display name
    var chargeStateName: String {
        switch chargeState {
        case 0: return "OFF"
        case 2: return "FAULT"
        case 3: return "BULK"
        case 4: return "ABSORPTION"
        case 5: return "FLOAT"
        case 7: return "EQUALIZE"
        default: return "État \(chargeState)"
        }
    }

    /// Color matching the VE.Direct charge state
    var chargeStateColor: Color {
        switch chargeState {
        case 0: return .gray
        case 2: return .red
        case 3: return .orange
        case 4: return .yellow
        case 5: return .green
        case 7: return .blue
        default: return .gray
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
