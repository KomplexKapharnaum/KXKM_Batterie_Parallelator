import SwiftUI
import Shared

struct SystemView: View {
    @EnvironmentObject var vm: SystemViewModel

    var body: some View {
        NavigationStack {
            List {
                if let sys = vm.system {
                    Section("Firmware") {
                        row("Version", sys.firmwareVersion)
                        row("Uptime", formatUptime(sys.uptimeSeconds))
                        row("Heap libre", formatBytes(sys.heapFree))
                    }

                    Section("Topologie") {
                        row("INA237", "\(sys.nbIna)")
                        row("TCA9535", "\(sys.nbTca)")
                        HStack {
                            Text("Validation")
                            Spacer()
                            Image(systemName: sys.topologyValid ? "checkmark.circle.fill" : "xmark.circle.fill")
                                .foregroundColor(sys.topologyValid ? .green : .red)
                        }
                    }

                    Section("WiFi") {
                        row("IP", sys.wifiIp ?? "Non connecté")
                    }
                } else {
                    Section {
                        ProgressView("Chargement...")
                    }
                }

                if let solar = vm.solar {
                    Section("Solaire (VE.Direct)") {
                        row("Tension panneau", "\(Double(solar.panelVoltageMv) / 1000.0, specifier: "%.1f") V")
                        row("Puissance", "\(solar.panelPowerW) W")
                        row("Tension batterie", "\(Double(solar.batteryVoltageMv) / 1000.0, specifier: "%.1f") V")
                        row("Courant", "\(Double(solar.batteryCurrentMa) / 1000.0, specifier: "%.2f") A")
                        row("État charge", chargeStateName(solar.chargeState))
                        row("Production jour", "\(solar.yieldTodayWh) Wh")
                    }
                }
            }
            .navigationTitle("Système")
        }
    }

    private func row(_ label: String, _ value: String) -> some View {
        HStack {
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

    private func chargeStateName(_ cs: Int32) -> String {
        switch cs {
        case 0: return "Off"
        case 2: return "Fault"
        case 3: return "Bulk"
        case 4: return "Absorption"
        case 5: return "Float"
        default: return "État \(cs)"
        }
    }
}
