import SwiftUI

struct ConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        NavigationStack {
            List {
                NavigationLink("Protection") {
                    ProtectionConfigView().environmentObject(vm)
                }
                NavigationLink("WiFi BMU") {
                    WifiConfigView().environmentObject(vm)
                }
                NavigationLink("MQTT") {
                    MqttConfigView().environmentObject(vm)
                }
                NavigationLink("Nom BMU") {
                    DeviceNameView().environmentObject(vm)
                }
                NavigationLink("Utilisateurs") {
                    UserManagementView().environmentObject(vm)
                }
                NavigationLink("Sync cloud") {
                    SyncConfigView().environmentObject(vm)
                }
                NavigationLink("Transport") {
                    TransportConfigView().environmentObject(vm)
                }
            }
            .navigationTitle("Configuration")
        }
    }
}
