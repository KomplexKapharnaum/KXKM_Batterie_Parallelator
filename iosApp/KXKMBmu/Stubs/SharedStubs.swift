/// Stubs replacing KMP Shared module — to be removed when real framework is built.
/// These types mirror the KMP model/ and domain/ classes.

import Foundation
import SwiftUI

// MARK: - Model types

enum BatteryStatus: String, CaseIterable {
    case connected, disconnected, reconnecting, error, locked
}

struct BatteryState: Identifiable {
    var id: Int { index }
    let index: Int
    let voltageMv: Int32
    let currentMa: Int32
    let state: BatteryStatus
    let ahDischargeMah: Int32
    let ahChargeMah: Int32
    let nbSwitch: Int
}

struct SystemInfo {
    let firmwareVersion: String
    let heapFree: Int64
    let uptimeSeconds: Int64
    let wifiIp: String?
    let nbIna: Int
    let nbTca: Int
    let topologyValid: Bool
}

struct SolarData {
    let batteryVoltageMv: Int
    let batteryCurrentMa: Int
    let panelVoltageMv: Int
    let panelPowerW: Int
    let chargeState: Int32
    let errorCode: UInt8
    let yieldTodayWh: Int64
    let isValid: Bool
}

struct AuditEvent: Identifiable {
    var id: Int64 { timestamp }
    let timestamp: Int64
    let userId: String
    let action: String
    let target: NSNumber?
    let detail: String?
}

enum UserRole: String, CaseIterable {
    case admin, technician, viewer
}

struct UserProfile: Identifiable {
    let id: String
    let name: String
    let role: UserRole
    let pinHash: String
    let salt: String
}

enum TransportChannel: String, CaseIterable {
    case ble, wifi, mqttCloud, restCloud, offline
}

struct ProtectionConfig {
    var minMv: Int = 24000
    var maxMv: Int = 30000
    var maxMa: Int = 10000
    var diffMv: Int = 1000
}

struct CommandResult {
    let isSuccess: Bool
    let errorMessage: String?
    static func ok() -> CommandResult { CommandResult(isSuccess: true, errorMessage: nil) }
    static func error(_ msg: String) -> CommandResult { CommandResult(isSuccess: false, errorMessage: msg) }
}

struct WifiStatusInfo {
    let ssid: String
    let ip: String
    let rssi: Int
    let connected: Bool
}

struct BatteryHistoryPoint: Identifiable {
    var id: Int64 { timestamp }
    let timestamp: Int64
    let voltageMv: Int
    let currentMa: Int
}

// MARK: - Mock Use Cases

class MonitoringUseCase {
    private func makeMockBattery(_ i: Int) -> BatteryState {
        let v = Int32(25000 + i * 500 + Int.random(in: -200...200))
        let c = Int32(1000 + i * 200 + Int.random(in: -100...100))
        let s: BatteryStatus = i == 3 ? .disconnected : .connected
        return BatteryState(
            index: i, voltageMv: v, currentMa: c, state: s,
            ahDischargeMah: Int32(1500 + i * 300),
            ahChargeMah: Int32(200 + i * 50),
            nbSwitch: i + 1
        )
    }

    func batteries() async -> [BatteryState] {
        (0..<4).map { makeMockBattery($0) }
    }

    func battery(index: Int) async -> BatteryState? {
        await batteries().first { $0.index == index }
    }

    func system() async -> SystemInfo {
        SystemInfo(
            firmwareVersion: "0.5.0", heapFree: 16800000,
            uptimeSeconds: 3600, wifiIp: nil,
            nbIna: 0, nbTca: 0, topologyValid: false
        )
    }

    func solar() async -> SolarData? {
        nil
    }

    func getHistory(batteryIndex: Int, hours: Int) async -> [BatteryHistoryPoint] {
        let now = Int64(Date().timeIntervalSince1970 * 1000)
        return (0..<100).map { i in
            BatteryHistoryPoint(
                timestamp: now - Int64(i) * 360_000,
                voltageMv: 25500 + Int.random(in: -500...500),
                currentMa: 1200 + Int.random(in: -300...300)
            )
        }.reversed()
    }
}

class ControlUseCase {
    func switchBattery(index: Int, on: Bool) async -> CommandResult {
        try? await Task.sleep(nanoseconds: 500_000_000)
        return .ok()
    }

    func resetSwitchCount(index: Int) async -> CommandResult {
        try? await Task.sleep(nanoseconds: 300_000_000)
        return .ok()
    }
}

class ConfigUseCase {
    func getCurrentConfig() -> ProtectionConfig { ProtectionConfig() }

    func setProtectionConfig(minMv: Int32, maxMv: Int32, maxMa: Int32, diffMv: Int32) async -> CommandResult {
        .ok()
    }

    func setWifiConfig(ssid: String, password: String) async -> CommandResult {
        .error("BLE non connecté (mock)")
    }

    func getPendingSyncCount() -> Int64 { 0 }
}

class AuditUseCase {
    func getEvents(action: String?, batteryIndex: Int?) async -> [AuditEvent] {
        let all = [
            AuditEvent(timestamp: Int64(Date().timeIntervalSince1970 * 1000) - 60000,
                      userId: "admin", action: "switch_on", target: 0, detail: nil),
            AuditEvent(timestamp: Int64(Date().timeIntervalSince1970 * 1000) - 120000,
                      userId: "admin", action: "config_change", target: nil,
                      detail: "V_min: 24000→25000"),
        ]
        return all.filter { event in
            if let action, !event.action.hasPrefix(action) { return false }
            if let batteryIndex, event.target?.intValue != batteryIndex { return false }
            return true
        }
    }

    func getPendingSyncCount() -> Int64 { 2 }
}

class AuthUseCase {
    private var users: [UserProfile] = []

    func hasNoUsers() -> Bool { users.isEmpty }

    func authenticate(pin: String) -> UserProfile? {
        // Mock: any 6-digit PIN works
        if pin.count == 6 {
            return users.first ?? UserProfile(id: "admin", name: "Admin", role: .admin, pinHash: "", salt: "")
        }
        return nil
    }

    func createUser(name: String, pin: String, role: UserRole) {
        users.append(UserProfile(id: UUID().uuidString, name: name, role: role, pinHash: "", salt: ""))
    }

    func deleteUser(userId: String) {
        users.removeAll { $0.id == userId }
    }

    func getAllUsers() -> [UserProfile] { users }
}

class TransportManager {
    var activeChannel: TransportChannel = .offline
}

// MARK: - Factory (replaces SharedFactory)

enum SharedFactory {
    static let companion = SharedFactoryCompanion()
}

class SharedFactoryCompanion {
    private let auth = AuthUseCase()
    func createMonitoringUseCase() -> MonitoringUseCase { MonitoringUseCase() }
    func createControlUseCase() -> ControlUseCase { ControlUseCase() }
    func createConfigUseCase() -> ConfigUseCase { ConfigUseCase() }
    func createAuditUseCase() -> AuditUseCase { AuditUseCase() }
    func createAuthUseCase() -> AuthUseCase { auth }
    func createTransportManager() -> TransportManager { TransportManager() }
}

// MARK: - GATT extended models

struct RintResult {
    let rOhmicMohm: Float
    let rTotalMohm: Float
    let valid: Bool
}

struct SohResult {
    let sohPercent: Float
    let confidence: Float
    let rintMohm: Float
    let rintValid: Bool
}

struct BalancerInfo {
    let isOff: Bool
    let balancing: Bool
    let dutyPct: Int
}

struct VictronDevice: Identifiable {
    let id: String
    let recordType: UInt8
    let mac: Data
    let decrypted: Bool
    let rawData: Data
}
