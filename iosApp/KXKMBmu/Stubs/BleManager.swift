/// CoreBluetooth BLE manager — connects to KXKM-BMU and parses GATT data.
/// Replaces mock data in SharedStubs when a real BMU is in range.

import Foundation
import CoreBluetooth
import SwiftUI
import os.log

private let bleLog = Logger(subsystem: "com.kxkm.bmu", category: "BLE")

// MARK: - KXKM BMU UUIDs

private func bmuUUID(_ suffix: UInt16) -> CBUUID {
    let s = String(format: "4B584B4D-%04X-4B4D-424D-55424C450000", suffix)
    return CBUUID(string: s)
}

private let kBatterySvcUUID  = bmuUUID(0x0001)
private let kSystemSvcUUID   = bmuUUID(0x0002)
private let kControlSvcUUID  = bmuUUID(0x0003)

private func batteryCharUUID(_ index: Int) -> CBUUID { bmuUUID(UInt16(0x0010 + index)) }

private let kFwVersionUUID   = bmuUUID(0x0020)
private let kHeapFreeUUID    = bmuUUID(0x0021)
private let kUptimeUUID      = bmuUUID(0x0022)
private let kWifiIpUUID      = bmuUUID(0x0023)
private let kTopologyUUID    = bmuUUID(0x0024)
private let kSolarUUID       = bmuUUID(0x0025)

private let kSwitchUUID      = bmuUUID(0x0030)
private let kResetUUID       = bmuUUID(0x0031)
private let kConfigUUID      = bmuUUID(0x0032)
private let kStatusUUID      = bmuUUID(0x0033)
private let kWifiCfgUUID     = bmuUUID(0x0034)
private let kWifiStsUUID     = bmuUUID(0x0035)

// MARK: - Timing constants

private enum BLEConstants {
    static let scanTimeoutSeconds: TimeInterval = 15
    static let rssiPollIntervalSeconds: TimeInterval = 5
    static let reconnectDelaySeconds: TimeInterval = 2
    static let pollIntervalSeconds: TimeInterval = 2
}

// MARK: - Discovered device

struct DiscoveredBMU: Identifiable, Hashable {
    let id: UUID
    let name: String
    let deviceName: String
    let rssi: Int
    let peripheral: CBPeripheral

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (lhs: DiscoveredBMU, rhs: DiscoveredBMU) -> Bool { lhs.id == rhs.id }
}

// MARK: - BLE Manager

class BleManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    static let shared = BleManager()

    @Published var isConnected = false
    @Published var isScanning = false
    @Published var discoveredDevices: [DiscoveredBMU] = []
    @Published var connectedDeviceName: String? = nil
    @Published var batteries: [BatteryState] = []
    @Published var systemInfo: SystemInfo?
    @Published var solarData: SolarData?
    @Published var lastCommandResult: CommandResult?
    @Published var rssi: Int = 0
    @Published var bleDebugLog: [String] = []

    private lazy var central = CBCentralManager(delegate: self, queue: nil)
    private var peripheral: CBPeripheral?
    private var controlChars: [CBUUID: CBCharacteristic] = [:]
    /// Battery characteristics keyed by index — for periodic re-read
    private var batteryChars: [Int: CBCharacteristic] = [:]
    /// System characteristics for periodic re-read
    private var systemChars: [CBCharacteristic] = []
    /// Timer for polling reads (workaround firmware notify_custom(0xFFFF) bug)
    private var pollTimer: Timer?

    override init() {
        super.init()
        _ = central // trigger lazy init
    }

    private func logBle(_ msg: String) {
        bleLog.info("\(msg)")
        bleDebugLog.append(msg)
        if bleDebugLog.count > 40 { bleDebugLog.removeFirst() }
    }

    // MARK: - Public API

    func startScan() {
        guard central.state == .poweredOn else { return }
        isScanning = true
        discoveredDevices = []
        central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: false])
        DispatchQueue.main.asyncAfter(deadline: .now() + BLEConstants.scanTimeoutSeconds) { [weak self] in
            guard let self, self.isScanning else { return }
            self.stopScan()
        }
    }

    func stopScan() {
        central.stopScan()
        isScanning = false
    }

    func connectTo(_ device: DiscoveredBMU) {
        stopScan()
        peripheral = device.peripheral
        peripheral?.delegate = self
        connectedDeviceName = device.deviceName
        logBle("Connecting to \(device.name)...")
        central.connect(device.peripheral, options: nil)
    }

    func disconnect() {
        stopPollTimer()
        if let p = peripheral {
            central.cancelPeripheralConnection(p)
        }
        connectedDeviceName = nil
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

    // MARK: - Poll timer (workaround for firmware notify bug)

    private func startPollTimer() {
        stopPollTimer()
        pollTimer = Timer.scheduledTimer(withTimeInterval: BLEConstants.pollIntervalSeconds, repeats: true) { [weak self] _ in
            self?.pollAllCharacteristics()
        }
    }

    private func stopPollTimer() {
        pollTimer?.invalidate()
        pollTimer = nil
    }

    private func pollAllCharacteristics() {
        guard let p = peripheral, isConnected else { return }
        for (_, char) in batteryChars {
            p.readValue(for: char)
        }
        for char in systemChars {
            p.readValue(for: char)
        }
    }

    // MARK: - CBCentralManagerDelegate
    // All delegate methods run on main queue (CBCentralManager initialized with queue: nil)

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        logBle("BT state: \(central.state.rawValue) (\(central.state == .poweredOn ? "ON" : "not ready"))")
        if central.state == .poweredOn {
            startScan()
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = peripheral.name ?? advertisementData[CBAdvertisementDataLocalNameKey] as? String ?? ""
        guard name.hasPrefix("KXKM-BMU") else { return }

        let deviceName = name.hasPrefix("KXKM-BMU-")
            ? String(name.dropFirst("KXKM-BMU-".count))
            : name

        let discovered = DiscoveredBMU(
            id: peripheral.identifier,
            name: name,
            deviceName: deviceName,
            rssi: RSSI.intValue,
            peripheral: peripheral
        )

        if let idx = discoveredDevices.firstIndex(where: { $0.id == discovered.id }) {
            discoveredDevices[idx] = discovered
        } else {
            discoveredDevices.append(discovered)
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        logBle("Connected to \(peripheral.name ?? "?")")
        isConnected = true
        batteryChars = [:]
        systemChars = []
        logBle("Discovering ALL services...")
        // Discover ALL services (nil) — avoids CoreBluetooth UUID cache issues with 128-bit customs
        peripheral.discoverServices(nil)
        peripheral.readRSSI()
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        logBle("CONNECT FAILED: \(error?.localizedDescription ?? "unknown")")
        isConnected = false
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        logBle("Disconnected: \(error?.localizedDescription ?? "clean")")
        isConnected = false
        connectedDeviceName = nil
        batteries = []
        controlChars = [:]
        batteryChars = [:]
        systemChars = []
        stopPollTimer()
        DispatchQueue.main.asyncAfter(deadline: .now() + BLEConstants.reconnectDelaySeconds) { [weak self] in
            self?.startScan()
        }
    }

    // MARK: - CBPeripheralDelegate

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            logBle("Service discovery ERROR: \(error.localizedDescription)")
            return
        }
        guard let services = peripheral.services, !services.isEmpty else {
            logBle("NO SERVICES found!")
            return
        }
        logBle("Found \(services.count) services")
        for svc in services {
            let uuid = svc.uuid.uuidString
            let short = uuid.count > 12 ? String(uuid.prefix(12)) + "..." : uuid
            logBle("  svc: \(short)")
            peripheral.discoverCharacteristics(nil, for: svc)
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error {
            logBle("Char error: \(error.localizedDescription)")
            return
        }
        guard let chars = service.characteristics, !chars.isEmpty else {
            logBle("No chars for svc \(service.uuid.uuidString.prefix(12))")
            return
        }

        let svcUUID = service.uuid
        let isBatSvc = svcUUID == kBatterySvcUUID
        let isSysSvc = svcUUID == kSystemSvcUUID
        let isCtlSvc = svcUUID == kControlSvcUUID

        let svcName = isBatSvc ? "Battery" : isSysSvc ? "System" : isCtlSvc ? "Control" : "Unknown"
        logBle("\(svcName) svc: \(chars.count) chars")

        for char in chars {
            let props = char.properties
            let charUUID = char.uuid.uuidString
            let short = charUUID.count > 12 ? String(charUUID.prefix(12)) + "..." : charUUID
            logBle("  \(short) R=\(props.contains(.read)) N=\(props.contains(.notify)) W=\(props.contains(.write))")

            // Store battery characteristics for polling
            if isBatSvc {
                for i in 0..<16 {
                    if char.uuid == batteryCharUUID(i) {
                        batteryChars[i] = char
                        break
                    }
                }
            }

            // Store system characteristics for polling
            if isSysSvc && props.contains(.read) {
                systemChars.append(char)
            }

            if props.contains(.notify) {
                peripheral.setNotifyValue(true, for: char)
            }
            // Read system + control chars immediately, but defer battery reads
            // to poll timer (after topology trim to avoid Invalid Handle errors)
            if !isBatSvc && props.contains(.read) {
                peripheral.readValue(for: char)
            }
            if isCtlSvc {
                controlChars[char.uuid] = char
            }
        }

        // Start poll timer once we have battery characteristics
        if isBatSvc && !batteryChars.isEmpty {
            logBle("Starting poll timer (\(Int(BLEConstants.pollIntervalSeconds))s) for \(batteryChars.count) batteries")
            startPollTimer()
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            logBle("READ ERR \(characteristic.uuid.uuidString.prefix(12)): \(error.localizedDescription)")
            return
        }
        guard let data = characteristic.value, !data.isEmpty else {
            logBle("Empty data: \(characteristic.uuid.uuidString.prefix(12))")
            return
        }

        let uuid = characteristic.uuid
        // Use parent service to disambiguate — battery chars 0x0020-0x002F
        // collide with system chars 0x0020-0x0025
        let parentSvcUUID = characteristic.service?.uuid

        // Battery characteristics from Battery service only
        if parentSvcUUID == kBatterySvcUUID {
            for i in 0..<16 {
                if uuid == batteryCharUUID(i) {
                    if let state = parseBattery(index: i, data: data) {
                        logBle("Bat[\(i)]: \(state.voltageMv)mV \(state.currentMa)mA \(state.state.rawValue)")
                        if let idx = batteries.firstIndex(where: { $0.index == i }) {
                            batteries[idx] = state
                        } else {
                            batteries.append(state)
                            batteries.sort { $0.index < $1.index }
                        }
                    } else {
                        logBle("Bat[\(i)] parse FAIL (\(data.count)B)")
                    }
                    return
                }
            }
            // Battery char > 15 or R_int/SOH — ignore for now
            return
        }

        // System characteristics from System service only
        if parentSvcUUID == kSystemSvcUUID {
            switch uuid {
            case kFwVersionUUID:
                let fw = String(data: data, encoding: .utf8) ?? "?"
                logBle("FW: \(fw)")
                systemInfo = SystemInfo(
                    firmwareVersion: fw,
                    heapFree: systemInfo?.heapFree ?? 0,
                    uptimeSeconds: systemInfo?.uptimeSeconds ?? 0,
                    wifiIp: systemInfo?.wifiIp,
                    nbIna: systemInfo?.nbIna ?? 0,
                    nbTca: systemInfo?.nbTca ?? 0,
                    topologyValid: systemInfo?.topologyValid ?? false
                )
            case kHeapFreeUUID:
                if data.count >= 4 {
                    let heap = Int64(data.readUInt32LE(at: 0))
                    systemInfo = SystemInfo(
                        firmwareVersion: systemInfo?.firmwareVersion ?? "?",
                        heapFree: heap,
                        uptimeSeconds: systemInfo?.uptimeSeconds ?? 0,
                        wifiIp: systemInfo?.wifiIp,
                        nbIna: systemInfo?.nbIna ?? 0,
                        nbTca: systemInfo?.nbTca ?? 0,
                        topologyValid: systemInfo?.topologyValid ?? false
                    )
                }
            case kUptimeUUID:
                if data.count >= 4 {
                    let up = Int64(data.readUInt32LE(at: 0))
                    systemInfo = SystemInfo(
                        firmwareVersion: systemInfo?.firmwareVersion ?? "?",
                        heapFree: systemInfo?.heapFree ?? 0,
                        uptimeSeconds: up,
                        wifiIp: systemInfo?.wifiIp,
                        nbIna: systemInfo?.nbIna ?? 0,
                        nbTca: systemInfo?.nbTca ?? 0,
                        topologyValid: systemInfo?.topologyValid ?? false
                    )
                }
            case kWifiIpUUID:
                let ip = String(data: data, encoding: .utf8)?.trimmingCharacters(in: .controlCharacters)
                systemInfo = SystemInfo(
                    firmwareVersion: systemInfo?.firmwareVersion ?? "?",
                    heapFree: systemInfo?.heapFree ?? 0,
                    uptimeSeconds: systemInfo?.uptimeSeconds ?? 0,
                    wifiIp: ip,
                    nbIna: systemInfo?.nbIna ?? 0,
                    nbTca: systemInfo?.nbTca ?? 0,
                    topologyValid: systemInfo?.topologyValid ?? false
                )
            case kTopologyUUID:
                if data.count >= 3 {
                    let nbIna = Int(data[0])
                    logBle("Topo: INA=\(nbIna) TCA=\(data[1]) valid=\(data[2])")
                    systemInfo = SystemInfo(
                        firmwareVersion: systemInfo?.firmwareVersion ?? "?",
                        heapFree: systemInfo?.heapFree ?? 0,
                        uptimeSeconds: systemInfo?.uptimeSeconds ?? 0,
                        wifiIp: systemInfo?.wifiIp,
                        nbIna: nbIna,
                        nbTca: Int(data[1]),
                        topologyValid: data[2] != 0
                    )
                    // Trim battery poll list to real INA count
                    let validKeys = batteryChars.keys.filter { $0 < nbIna }
                    let invalidKeys = batteryChars.keys.filter { $0 >= nbIna }
                    if !invalidKeys.isEmpty {
                        for k in invalidKeys { batteryChars.removeValue(forKey: k) }
                        logBle("Poll trimmed to \(validKeys.count) batteries")
                    }
                }
            case kSolarUUID:
                // Firmware ble_solar_char_t: 15 bytes packed
                // [0:2] battery_voltage_mv, [2:2] battery_current_ma,
                // [4:2] panel_voltage_mv, [6:2] panel_power_w,
                // [8:1] charge_state, [9:1] error_code,
                // [10:4] yield_today_wh, [14:1] valid
                if data.count >= 15 {
                    let valid = data[14] != 0
                    solarData = SolarData(
                        batteryVoltageMv: Int(data.readInt16LE(at: 0)),
                        batteryCurrentMa: Int(data.readInt16LE(at: 2)),
                        panelVoltageMv: Int(data.readUInt16LE(at: 4)),
                        panelPowerW: Int(data.readUInt16LE(at: 6)),
                        chargeState: Int32(data[8]),
                        errorCode: data[9],
                        yieldTodayWh: Int64(data.readUInt32LE(at: 10)),
                        isValid: valid
                    )
                }
            default:
                break
            }
            return
        }

        // Control service — status response
        if parentSvcUUID == kControlSvcUUID {
            if uuid == kStatusUUID && data.count >= 3 {
                let result = data[2]
                lastCommandResult = result == 0 ? .ok() : .error("code=\(result)")
            }
            return
        }

        // Unknown service characteristic
        logBle("Unknown char \(uuid.uuidString.prefix(12)) from svc \(parentSvcUUID?.uuidString.prefix(12) ?? "nil")")
    }

    func peripheral(_ peripheral: CBPeripheral, didReadRSSI RSSI: NSNumber, error: Error?) {
        rssi = RSSI.intValue
        DispatchQueue.main.asyncAfter(deadline: .now() + BLEConstants.rssiPollIntervalSeconds) { [weak self] in
            self?.peripheral?.readRSSI()
        }
    }

    // MARK: - GATT parsing

    private func parseBattery(index: Int, data: Data) -> BatteryState? {
        // Firmware ble_battery_char_t: 18 bytes packed
        // [0:4] voltage_mv, [4:4] current_ma, [8:1] state,
        // [9:4] ah_discharge_mah, [13:4] ah_charge_mah, [17:1] nb_switch
        guard data.count >= 15 else {
            logBle("Bat[\(index)] too short: \(data.count)B")
            return nil
        }
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
