import SwiftUI
import Shared

struct TransportConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        Form {
            Section("Canal actif") {
                HStack {
                    Image(systemName: vm.activeChannel.icon)
                    Text(vm.activeChannel.displayName)
                        .font(.body.bold())
                }
            }
            Section("Forcer un canal") {
                Picker("Canal", selection: $vm.forceChannel) {
                    Text("Automatique").tag(nil as TransportChannel?)
                    Text("BLE").tag(TransportChannel.ble as TransportChannel?)
                    Text("WiFi").tag(TransportChannel.wifi as TransportChannel?)
                    Text("Cloud").tag(TransportChannel.mqttCloud as TransportChannel?)
                }
                .pickerStyle(.segmented)

                Text("Auto = BLE > WiFi > Cloud > Offline")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .navigationTitle("Transport")
    }
}
