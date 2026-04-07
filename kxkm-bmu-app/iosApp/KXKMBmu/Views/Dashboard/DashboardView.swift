import SwiftUI
import Shared

struct DashboardView: View {
    @EnvironmentObject var vm: DashboardViewModel

    private let columns = [
        GridItem(.adaptive(minimum: 140, maximum: 200))
    ]

    @StateObject private var transport = TransportStatusViewModel()

    var body: some View {
        NavigationStack {
            ScrollView {
                if !transport.isConnected && !vm.batteries.isEmpty {
                    HStack {
                        Image(systemName: "wifi.slash")
                        Text("Offline — dernières valeurs connues")
                    }
                    .font(.caption)
                    .foregroundColor(.orange)
                    .padding(.vertical, 4)
                    .frame(maxWidth: .infinity)
                    .background(Color.orange.opacity(0.1))
                }

                if vm.isLoading {
                    ProgressView("Connexion au BMU...")
                        .padding(.top, 60)
                } else if vm.batteries.isEmpty {
                    VStack(spacing: 12) {
                        Image(systemName: "bolt.slash")
                            .font(.system(size: 40))
                            .foregroundColor(.secondary)
                        Text("Aucune batterie détectée")
                            .foregroundColor(.secondary)
                    }
                    .padding(.top, 60)
                } else {
                    LazyVGrid(columns: columns, spacing: 12) {
                        ForEach(vm.batteries, id: \.index) { battery in
                            NavigationLink(value: battery.index) {
                                BatteryCellView(battery: battery)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                    .padding()
                }
            }
            .navigationTitle("Batteries")
            .navigationDestination(for: Int32.self) { index in
                BatteryDetailView(batteryIndex: Int(index))
            }
        }
    }
}
