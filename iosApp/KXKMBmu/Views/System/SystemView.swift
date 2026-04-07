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
