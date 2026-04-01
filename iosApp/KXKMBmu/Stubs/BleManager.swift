/// CoreBluetooth BLE manager — connects to KXKM-BMU and parses GATT data.
/// Replaces mock data in SharedStubs when a real BMU is in range.

import Foundation
import CoreBluetooth
import SwiftUI

// MARK: - KXKM BMU UUIDs

private func bmuUUID(_ suffix: UInt16) -> CBUUID {
    // Base: 4b584b4d-xxxx-4b4d-424d-55424c450000
    // suffix inserted at bytes 4-5 (big-endian)
    let hi = UInt8((suffix >> 8) & 0xFF)
    let lo = UInt8(suffix & 0xFF)
    let s = String(format: "4B584B4D-%02X%02X-4B4D-424D-55424C450000", lo, hi)
    return CBUUID(string: s)
}

private let kBatterySvcUUID  = bmuUUID(0x0001)
private let kSystemSvcUUID   = bmuUUID(0x0002)
private let kControlSvcUUID  = bmuUUID(0x0003)

// Battery chars 0x0010–0x001F
private func batteryCharUUID(_ index: Int) -> CBUUID { bmuUUID(UInt16(0x0010 + index)) }

// System chars
private let kFwVersionUUID   = bmuUUID(0x0020)
private let kHeapFreeUUID    = bmuUUID(0x0021)
private let kUptimeUUID      = bmuUUID(0x0022)
private let kWifiIpUUID      = bmuUUID(0x0023)
private let kTopologyUUID    = bmuUUID(0x0024)
private let kSolarUUID       = bmuUUID(0x0025)

// Control chars
private let kSwitchUUID      = bmuUUID(0x0030)
private let kResetUUID       = bmuUUID(0x0031)
private let kConfigUUID      = bmuUUID(0x0032)
private let kStatusUUID      = bmuUUID(0x0033)
private let kWifiCfgUUID     = bmuUUID(0x0034)
private let kWifiStsUUID     = bmuUUID(0x0035)

// MARK: - BLE Manager

class BleManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    static let shared = BleManager()

    @Published var isConnected = false
    @Published var isScanning = false
    @Published var batteries: [BatteryState] = []
    @Published var systemInfo: SystemInfo?
    @Published var solarData: SolarData?
    @Published var lastCommandResult: CommandResult?
    @Published var rssi: Int = 0

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var controlChars: [CBUUID: CBCharacteristic] = [:]

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: nil)
    }

    // MARK: - Public API

    func startScan() {
        guard central.state == .poweredOn else { return }
        isScanning = true
        central.scanForPeripherals(withServices: [kBatterySvcUUID], options: nil)
        // Timeout scan after 10s
        DispatchQueue.main.asyncAfter(deadline: .now() + 10) { [weak self] in
            guard let self, self.isScanning else { return }
            self.central.stopScan()
            self.isScanning = false
        }
    }

    func disconnect() {
        if let p = peripheral {
            central.cancelPeripheralConnection(p)
        }
    }

    func switchBattery(index: Int, on: Bool) {
        guard let char = controlChars[kSwitchUUID] else {
            lastCommandResult = .error("BLE non connecté")
            return
        }
        let data = Data([UInt8(index), on ? 1 : 0])
        peripheral?.writeValue(data, for: char, type: .withResponse)
    }

    func resetSwitchCount(index: Int) {
        guard let char = controlChars[kResetUUID] else {
            lastCommandResult = .error("BLE non connecté")
            return
        }
        let data = Data([UInt8(index)])
        peripheral?.writeValue(data, for: char, type: .withResponse)
    }

    func setProtectionConfig(_ config: ProtectionConfig) {
        guard let char = controlChars[kConfigUUID] else {
            lastCommandResult = .error("BLE non connecté")
            return
        }
        var data = Data(count: 8)
        data.writeUInt16LE(config.minMv, at: 0)
        data.writeUInt16LE(config.maxMv, at: 2)
        data.writeUInt16LE(config.maxMa, at: 4)
        data.writeUInt16LE(config.diffMv, at: 6)
        peripheral?.writeValue(data, for: char, type: .withResponse)
    }

    func setWifiConfig(ssid: String, password: String) {
        guard let char = controlChars[kWifiCfgUUID] else {
            lastCommandResult = .error("BLE non connecté")
            return
        }
        var buf = Data(count: 96)
        if let ssidData = ssid.data(using: .utf8) {
            buf.replaceSubrange(0..<min(ssidData.count, 32), with: ssidData)
        }
        if let passData = password.data(using: .utf8) {
            buf.replaceSubrange(32..<(32 + min(passData.count, 64)), with: passData)
        }
        peripheral?.writeValue(buf, for: char, type: .withResponse)
    }

    // MARK: - CBCentralManagerDelegate

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state == .poweredOn {
            startScan()
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = peripheral.name ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? ""
        guard name == "KXKM-BMU" else { return }

        central.stopScan()
        isScanning = false
        self.peripheral = peripheral
        peripheral.delegate = self
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        isConnected = true
        peripheral.discoverServices([kBatterySvcUUID, kSystemSvcUUID, kControlSvcUUID])
        peripheral.readRSSI()
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        isConnected = false
        batteries = []
        controlChars = [:]
        // Auto-reconnect after 2s
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in
            self?.startScan()
        }
    }

    // MARK: - CBPeripheralDelegate

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let services = peripheral.services else { return }
        for svc in services {
            peripheral.discoverCharacteristics(nil, for: svc)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        guard let chars = service.characteristics else { return }
        for char in chars {
            // Subscribe to notifications
            if char.properties.contains(.notify) {
                peripheral.setNotifyValue(true, for: char)
            }
            // Read initial values
            if char.properties.contains(.read) {
                peripheral.readValue(for: char)
            }
            // Store control chars for writing
            if service.uuid == kControlSvcUUID {
                controlChars[char.uuid] = char
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard let data = characteristic.value, error == nil else { return }
        let uuid = characteristic.uuid

        // Battery characteristics (0x0010–0x001F)
        for i in 0..<16 {
            if uuid == batteryCharUUID(i) {
                if let state = parseBattery(index: i, data: data) {
                    DispatchQueue.main.async {
                        if let idx = self.batteries.firstIndex(where: { $0.index == i }) {
                            self.batteries[idx] = state
                        } else {
                            self.batteries.append(state)
                            self.batteries.sort { $0.index < $1.index }
                        }
                    }
                }
                return
            }
        }

        // System chars
        DispatchQueue.main.async {
            switch uuid {
            case kFwVersionUUID:
                let fw = String(data: data, encoding: .utf8) ?? "?"
                self.systemInfo = SystemInfo(
                    firmwareVersion: fw,
                    heapFree: self.systemInfo?.heapFree ?? 0,
                    uptimeSeconds: self.systemInfo?.uptimeSeconds ?? 0,
                    wifiIp: self.systemInfo?.wifiIp,
                    nbIna: self.systemInfo?.nbIna ?? 0,
                    nbTca: self.systemInfo?.nbTca ?? 0,
                    topologyValid: self.systemInfo?.topologyValid ?? false
                )
            case kHeapFreeUUID:
                if data.count >= 4 {
                    let heap = Int64(data.readUInt32LE(at: 0))
                    self.systemInfo = SystemInfo(
                        firmwareVersion: self.systemInfo?.firmwareVersion ?? "?",
                        heapFree: heap,
                        uptimeSeconds: self.systemInfo?.uptimeSeconds ?? 0,
                        wifiIp: self.systemInfo?.wifiIp,
                        nbIna: self.systemInfo?.nbIna ?? 0,
                        nbTca: self.systemInfo?.nbTca ?? 0,
                        topologyValid: self.systemInfo?.topologyValid ?? false
                    )
                }
            case kUptimeUUID:
                if data.count >= 4 {
                    let up = Int64(data.readUInt32LE(at: 0))
                    self.systemInfo = SystemInfo(
                        firmwareVersion: self.systemInfo?.firmwareVersion ?? "?",
                        heapFree: self.systemInfo?.heapFree ?? 0,
                        uptimeSeconds: up,
                        wifiIp: self.systemInfo?.wifiIp,
                        nbIna: self.systemInfo?.nbIna ?? 0,
                        nbTca: self.systemInfo?.nbTca ?? 0,
                        topologyValid: self.systemInfo?.topologyValid ?? false
                    )
                }
            case kWifiIpUUID:
                let ip = String(data: data, encoding: .utf8)?.trimmingCharacters(in: .controlCharacters)
                self.systemInfo = SystemInfo(
                    firmwareVersion: self.systemInfo?.firmwareVersion ?? "?",
                    heapFree: self.systemInfo?.heapFree ?? 0,
                    uptimeSeconds: self.systemInfo?.uptimeSeconds ?? 0,
                    wifiIp: ip,
                    nbIna: self.systemInfo?.nbIna ?? 0,
                    nbTca: self.systemInfo?.nbTca ?? 0,
                    topologyValid: self.systemInfo?.topologyValid ?? false
                )
            case kTopologyUUID:
                if data.count >= 3 {
                    self.systemInfo = SystemInfo(
                        firmwareVersion: self.systemInfo?.firmwareVersion ?? "?",
                        heapFree: self.systemInfo?.heapFree ?? 0,
                        uptimeSeconds: self.systemInfo?.uptimeSeconds ?? 0,
                        wifiIp: self.systemInfo?.wifiIp,
                        nbIna: Int(data[0]),
                        nbTca: Int(data[1]),
                        topologyValid: data[2] != 0
                    )
                }
            case kSolarUUID:
                if data.count >= 12 {
                    self.solarData = SolarData(
                        batteryVoltageMv: Int(data.readInt16LE(at: 0)),
                        batteryCurrentMa: Int(data.readInt16LE(at: 2)),
                        panelVoltageMv: Int(data.readUInt16LE(at: 4)),
                        panelPowerW: Int(data.readUInt16LE(at: 6)),
                        chargeState: Int32(data[8]),
                        yieldTodayWh: Int64(data.readUInt32LE(at: 9))
                    )
                }
            case kStatusUUID:
                if data.count >= 3 {
                    let result = data[2]
                    self.lastCommandResult = result == 0 ? .ok() : .error("code=\(result)")
                }
            default:
                break
            }
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didReadRSSI RSSI: NSNumber, error: Error?) {
        DispatchQueue.main.async { self.rssi = RSSI.intValue }
        // Re-read RSSI every 5s
        DispatchQueue.main.asyncAfter(deadline: .now() + 5) { [weak self] in
            self?.peripheral?.readRSSI()
        }
    }

    // MARK: - GATT parsing

    private func parseBattery(index: Int, data: Data) -> BatteryState? {
        guard data.count >= 15 else { return nil }
        let voltage = data.readInt32LE(at: 0)
        let current = data.readInt32LE(at: 4)
        let stateRaw = Int(data[8])
        let ahDis = data.readInt32LE(at: 9)
        let ahCh = data.readInt32LE(at: 13)
        let nbSw = data.count > 17 ? Int(data[17]) : 0

        let states: [BatteryStatus] = [.connected, .disconnected, .reconnecting, .error, .locked]
        let state = stateRaw < states.count ? states[stateRaw] : .error

        return BatteryState(
            index: index,
            voltageMv: voltage,
            currentMa: current,
            state: state,
            ahDischargeMah: ahDis,
            ahChargeMah: ahCh,
            nbSwitch: nbSw
        )
    }
}

// MARK: - Data helpers (little-endian)

extension Data {
    func readInt32LE(at offset: Int) -> Int32 {
        guard count >= offset + 4 else { return 0 }
        return Int32(self[offset]) |
               (Int32(self[offset+1]) << 8) |
               (Int32(self[offset+2]) << 16) |
               (Int32(self[offset+3]) << 24)
    }

    func readUInt32LE(at offset: Int) -> UInt32 {
        guard count >= offset + 4 else { return 0 }
        return UInt32(self[offset]) |
               (UInt32(self[offset+1]) << 8) |
               (UInt32(self[offset+2]) << 16) |
               (UInt32(self[offset+3]) << 24)
    }

    func readInt16LE(at offset: Int) -> Int16 {
        guard count >= offset + 2 else { return 0 }
        return Int16(self[offset]) | (Int16(self[offset+1]) << 8)
    }

    func readUInt16LE(at offset: Int) -> UInt16 {
        guard count >= offset + 2 else { return 0 }
        return UInt16(self[offset]) | (UInt16(self[offset+1]) << 8)
    }

    mutating func writeUInt16LE(_ value: Int, at offset: Int) {
        guard count >= offset + 2 else { return }
        self[offset] = UInt8(value & 0xFF)
        self[offset+1] = UInt8((value >> 8) & 0xFF)
    }
}
