import SwiftUI

struct WifiConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel
    @State private var showPassword = false

    var body: some View {
        Form {
            Section("Configuration WiFi du BMU") {
                HStack {
                    TextField("SSID", text: $vm.wifiSsid)
                    Button {
                        vm.fetchCurrentSSID()
                    } label: {
                        Image(systemName: "wifi")
                            .foregroundColor(.accentColor)
                    }
                    .buttonStyle(.borderless)
                    .accessibilityLabel("Utiliser le WiFi actuel")
                }
                HStack {
                    if showPassword {
                        TextField("Mot de passe", text: $vm.wifiPassword)
                    } else {
                        SecureField("Mot de passe", text: $vm.wifiPassword)
                    }
                    Button {
                        showPassword.toggle()
                    } label: {
                        Image(systemName: showPassword ? "eye.slash" : "eye")
                            .foregroundColor(.secondary)
                    }
                    .buttonStyle(.borderless)
                }
                if !vm.wifiPassword.isEmpty,
                   WifiKeychain.load(ssid: vm.wifiSsid) != nil {
                    Label("Mot de passe mémorisé", systemImage: "key.fill")
                        .font(.caption)
                        .foregroundColor(.green)
                }
            }
            Section {
                Button("Envoyer via BLE") { vm.sendWifiConfig() }
                    .buttonStyle(.borderedProminent)
                Text("La config WiFi est envoyée via BLE. Le mot de passe est mémorisé localement.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            if let msg = vm.statusMessage {
                Section {
                    Text(msg)
                        .font(.caption)
                        .foregroundColor(.orange)
                }
            }
            if let status = vm.wifiStatus {
                Section("État WiFi du BMU") {
                    HStack {
                        Image(systemName: status.connected ? "wifi" : "wifi.slash")
                            .foregroundColor(status.connected ? .green : .red)
                        Text(status.connected ? "Connecté" : "Déconnecté")
                    }
                    if !status.ssid.isEmpty {
                        HStack {
                            Text("SSID")
                            Spacer()
                            Text(status.ssid).foregroundColor(.secondary)
                        }
                    }
                    if !status.ip.isEmpty {
                        HStack {
                            Text("IP")
                            Spacer()
                            Text(status.ip).foregroundColor(.secondary)
                        }
                    }
                    if status.connected {
                        HStack {
                            Text("RSSI")
                            Spacer()
                            Text("\(status.rssi) dBm").foregroundColor(.secondary)
                        }
                    }
                }
            }
        }
        .navigationTitle("WiFi BMU")
    }
}
