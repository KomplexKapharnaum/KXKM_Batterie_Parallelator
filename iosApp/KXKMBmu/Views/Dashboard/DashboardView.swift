import SwiftUI
// import Shared — using Stubs

struct DashboardView: View {
    @EnvironmentObject var vm: DashboardViewModel

    private let columns = [
        GridItem(.adaptive(minimum: 140, maximum: 200))
    ]

    private var connectedCount: Int {
        vm.batteries.filter { $0.state == .connected }.count
    }

    var body: some View {
        NavigationStack {
            ScrollView {
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
                    // Header: battery count summary
                    HStack {
                        VStack(alignment: .leading, spacing: 2) {
                            Text("\(vm.batteries.count) batteries")
                                .font(.headline)
                            Text("\(connectedCount) connectées")
                                .font(.subheadline)
                                .foregroundColor(connectedCount == vm.batteries.count ? .green : .orange)
                        }
                        Spacer()
                        Image(systemName: "bolt.circle.fill")
                            .font(.system(size: 28))
                            .foregroundColor(.green)
                    }
                    .padding(.horizontal)
                    .padding(.top, 8)

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
            .refreshable {
                await vm.refresh()
            }
            .navigationTitle("Batteries")
            .navigationDestination(for: Int32.self) { index in
                BatteryDetailView(batteryIndex: Int(index))
            }
        }
    }
}
