import SwiftUI

struct SystemView: View {
    @EnvironmentObject var vm: SystemViewModel

    var body: some View {
        NavigationStack {
            List {
                if let sys = vm.system {
                    Section("Firmware") {
                        iconRow("cpu", "Version", sys.firmwareVersion)
                        iconRow("clock", "Uptime", formatUptime(sys.uptimeSeconds))
                        iconRow("memorychip", "Heap libre", formatBytes(sys.heapFree))
                    }

                    Section("Topologie") {
                        iconRow("sensor", "INA237", "\(sys.nbIna)")
                        iconRow("rectangle.connected.to.line.below", "TCA9535", "\(sys.nbTca)")
                        HStack {
                            Image(systemName: "checkmark.shield")
                                .foregroundColor(.secondary)
                                .frame(width: 20)
                            Text("Validation")
                            Spacer()
                            Image(systemName: sys.topologyValid ? "checkmark.circle.fill" : "xmark.circle.fill")
                                .foregroundColor(sys.topologyValid ? .green : .red)
                        }
                    }

                    Section("WiFi") {
                        iconRow("antenna.radiowaves.left.and.right", "IP", sys.wifiIp ?? "Non connecté")
                    }
                } else {
                    Section {
                        ProgressView("Chargement...")
                    }
                }

                if !BleManager.shared.victronDevices.isEmpty {
                    Section("Victron") {
                        ForEach(BleManager.shared.victronDevices) { dev in
                            HStack {
                                Image(systemName: dev.recordType == 0x01 ? "sun.max" :
                                      dev.recordType == 0x02 ? "battery.75percent" : "bolt")
                                    .foregroundColor(dev.decrypted ? .green : .gray)
                                if dev.decrypted && dev.rawData.count >= 10 {
                                    VStack(alignment: .leading) {
                                        if dev.recordType == 0x01 {
                                            let ppv = Int(dev.rawData[4]) | (Int(dev.rawData[5]) << 8)
                                            let vbat = Float(Int(dev.rawData[8]) | (Int(dev.rawData[9]) << 8)) / 100.0
                                            Text(String(format: "%.1fV  %dW", vbat, ppv))
                                        } else if dev.recordType == 0x02 {
                                            let v = Float(Int(dev.rawData[2]) | (Int(dev.rawData[3]) << 8)) / 100.0
                                            let soc = (Int(dev.rawData[6]) | (Int(dev.rawData[7]) << 8)) / 10
                                            Text(String(format: "%.1fV  %d%%", v, soc))
                                        }
                                    }
                                } else {
                                    Text(dev.id).foregroundColor(.secondary)
                                    Text("(locked)").font(.caption).foregroundColor(.orange)
                                }
                            }
                        }
                    }
                }

                if let solar = vm.solar {
                    Section("Solaire (VE.Direct)") {
                        iconRow("sun.max", "Tension panneau", String(format: "%.1f V", Double(solar.panelVoltageMv) / 1000.0))
                        iconRow("bolt.fill", "Puissance", "\(solar.panelPowerW) W")
                        iconRow("battery.100", "Tension batterie", String(format: "%.1f V", Double(solar.batteryVoltageMv) / 1000.0))
                        iconRow("arrow.right", "Courant", String(format: "%.2f A", Double(solar.batteryCurrentMa) / 1000.0))
                        iconRow("gauge.medium", "État charge", solar.chargeStateName)
                        iconRow("chart.bar", "Production jour", "\(solar.yieldTodayWh) Wh")
                    }
                }
            }
            .navigationTitle("Système")
        }
    }

    private func iconRow(_ icon: String, _ label: String, _ value: String) -> some View {
        HStack {
            Image(systemName: icon)
                .foregroundColor(.secondary)
                .frame(width: 20)
            Text(label)
            Spacer()
            Text(value).foregroundColor(.secondary).monospacedDigit()
        }
    }

    private func formatUptime(_ seconds: Int64) -> String {
        let h = seconds / 3600
        let m = (seconds % 3600) / 60
        return "\(h)h \(m)m"
    }

    private func formatBytes(_ bytes: Int64) -> String {
        if bytes > 1_000_000 { return String(format: "%.1f MB", Double(bytes) / 1_000_000.0) }
        return String(format: "%.0f KB", Double(bytes) / 1000.0)
    }


}
