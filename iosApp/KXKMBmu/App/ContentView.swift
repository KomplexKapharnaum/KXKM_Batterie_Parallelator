import SwiftUI

struct ContentView: View {
    @EnvironmentObject var authVM: AuthViewModel
    @StateObject private var dashboardVM = DashboardViewModel()
    @StateObject private var systemVM = SystemViewModel()
    @StateObject private var auditVM = AuditViewModel()
    @StateObject private var configVM = ConfigViewModel()

    @ObservedObject private var ble = BleManager.shared

    var body: some View {
        VStack(spacing: 0) {
            StatusBarView()

            if ble.isConnected {
                TabView {
                    DashboardView()
                        .environmentObject(dashboardVM)
                        .tabItem {
                            Label("Batteries", systemImage: "bolt.fill")
                        }

                    SystemView()
                        .environmentObject(systemVM)
                        .tabItem {
                            Label("Système", systemImage: "gearshape")
                        }

                    AuditView()
                        .environmentObject(auditVM)
                        .tabItem {
                            Label("Audit", systemImage: "list.clipboard")
                        }

                    if authVM.currentUser?.role.canConfigure == true {
                        ConfigView()
                            .environmentObject(configVM)
                            .tabItem {
                                Label("Config", systemImage: "wrench")
                            }
                    }
                }
                .tint(.green)
            } else {
                DeviceSelectorView()
            }
            .tint(.green)
        }
        .preferredColorScheme(.dark)
    }
}
