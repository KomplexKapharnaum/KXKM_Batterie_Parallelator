import SwiftUI

struct WifiConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        Form {
            Section("Configuration WiFi du BMU") {
                TextField("SSID", text: $vm.wifiSsid)
                SecureField("Mot de passe", text: $vm.wifiPassword)
            }
            Section {
                Button("Envoyer via BLE") { vm.sendWifiConfig() }
                    .buttonStyle(.borderedProminent)
                Text("La config WiFi est envoyée via BLE uniquement.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            if let status = vm.wifiStatus {
                Section("État actuel") {
                    HStack {
                        Text("SSID")
                        Spacer()
                        Text(status.ssid).foregroundColor(.secondary)
                    }
                    HStack {
                        Text("IP")
                        Spacer()
                        Text(status.ip).foregroundColor(.secondary)
                    }
                    HStack {
                        Text("RSSI")
                        Spacer()
                        Text("\(status.rssi) dBm").foregroundColor(.secondary)
                    }
                }
            }
        }
        .navigationTitle("WiFi BMU")
    }
}
