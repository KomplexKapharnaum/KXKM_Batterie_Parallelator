import SwiftUI

struct DeviceNameView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        Form {
            Section("Nom de l'appareil") {
                TextField("Nom", text: $vm.deviceName)
                    .autocapitalization(.none)
                Text("Visible dans le BLE et les topics MQTT (bmu/{nom}/battery/N)")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            Section {
                Button("Envoyer via BLE") { vm.sendDeviceName() }
                    .buttonStyle(.borderedProminent)
            }
            if let msg = vm.statusMessage {
                Section { Text(msg).foregroundColor(.green) }
            }
        }
        .navigationTitle("Nom BMU")
    }
}
