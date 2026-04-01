import SwiftUI

struct SyncConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        Form {
            Section("kxkm-ai") {
                TextField("URL API", text: $vm.syncUrl)
                    .textContentType(.URL)
                    .keyboardType(.URL)
                TextField("Broker MQTT", text: $vm.mqttBroker)
            }
            Section("État") {
                HStack {
                    Text("En attente de sync")
                    Spacer()
                    Text("\(vm.syncPending)")
                        .foregroundColor(vm.syncPending > 0 ? .orange : .green)
                }
                if let last = vm.lastSyncTime {
                    HStack {
                        Text("Dernier sync")
                        Spacer()
                        Text(last, style: .relative).foregroundColor(.secondary)
                    }
                }
            }
        }
        .navigationTitle("Sync cloud")
    }
}
