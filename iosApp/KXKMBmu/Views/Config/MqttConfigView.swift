import SwiftUI

struct MqttConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        Form {
            Section("Broker MQTT") {
                TextField("URI", text: $vm.mqttUri)
                    .textContentType(.URL)
                    .keyboardType(.URL)
                    .textInputAutocapitalization(.never)
                Text("Ex: mqtt://192.168.0.1:1883")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            Section("Authentification") {
                TextField("Utilisateur", text: $vm.mqttUsername)
                    .textInputAutocapitalization(.never)
                SecureField("Mot de passe", text: $vm.mqttPassword)
            }
            Section {
                Button("Envoyer via BLE") { vm.sendMqttConfig() }
                    .buttonStyle(.borderedProminent)
                Text("La config MQTT est envoyee via BLE et sauvegardee dans le NVS du BMU.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            if let msg = vm.statusMessage {
                Section { Text(msg).foregroundColor(.green) }
            }
        }
        .navigationTitle("MQTT")
    }
}
