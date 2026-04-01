import SwiftUI
// import Shared — using Stubs

struct BatteryDetailView: View {
    @StateObject private var vm: BatteryDetailViewModel
    @EnvironmentObject var authVM: AuthViewModel
    @State private var showSwitchConfirm = false
    @State private var switchAction = true // true = ON

    init(batteryIndex: Int) {
        _vm = StateObject(wrappedValue: BatteryDetailViewModel(batteryIndex: batteryIndex))
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 24) {
                // Current state card
                if let bat = vm.battery {
                    stateCard(bat)
                }

                // Chart
                GroupBox("Historique tension (24h)") {
                    VoltageChartView(history: vm.history)
                }

                // Counters
                if let bat = vm.battery {
                    countersSection(bat)
                }

                // Controls (role-gated)
                if authVM.currentUser?.role.canControl == true {
                    controlsSection
                }

                // Command result
                if let result = vm.commandResult {
                    Text(result)
                        .font(.caption)
                        .foregroundColor(result == "OK" ? .green : .red)
                        .padding(.horizontal)
                }
            }
            .padding()
        }
        .navigationTitle("Batterie \(vm.batteryIndex + 1)")
        .confirmAction(
            isPresented: $showSwitchConfirm,
            title: switchAction ? "Connecter batterie ?" : "Déconnecter batterie ?",
            message: "Batterie \(vm.batteryIndex + 1) — cette action est enregistrée dans l'audit."
        ) {
            vm.switchBattery(on: switchAction)
        }
    }

    private func stateCard(_ bat: BatteryState) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                Text(bat.voltageMv.voltageDisplay)
                    .font(.system(.largeTitle, design: .monospaced).bold())
                Text(bat.currentMa.currentDisplay)
                    .font(.title3.monospacedDigit())
                    .foregroundColor(.secondary)
            }
            Spacer()
            VStack {
                BatteryStateIcon(state: bat.state)
                    .font(.title)
                Text(bat.state.displayName)
                    .font(.caption.bold())
                    .foregroundColor(bat.state.color)
            }
        }
        .padding()
        .background(
            LinearGradient(
                colors: [
                    bat.state.color.opacity(0.15),
                    Color(.systemGray6)
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
        )
        .cornerRadius(12)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(bat.state.color.opacity(0.4), lineWidth: 1)
        )
    }

    private func countersSection(_ bat: BatteryState) -> some View {
        GroupBox("Compteurs") {
            VStack(spacing: 8) {
                HStack {
                    Text("Décharge")
                    Spacer()
                    Text(bat.ahDischargeMah.ahDisplay)
                        .monospacedDigit()
                }
                HStack {
                    Text("Charge")
                    Spacer()
                    Text(bat.ahChargeMah.ahDisplay)
                        .monospacedDigit()
                }
                HStack {
                    Text("Nb switches")
                    Spacer()
                    Text("\(bat.nbSwitch)")
                        .monospacedDigit()
                }
            }
        }
    }

    private var controlsSection: some View {
        GroupBox("Contrôle") {
            HStack(spacing: 12) {
                Button {
                    switchAction = true
                    showSwitchConfirm = true
                } label: {
                    Label("Connecter", systemImage: "bolt.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(.green)

                Button {
                    switchAction = false
                    showSwitchConfirm = true
                } label: {
                    Label("Déconnecter", systemImage: "bolt.slash")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(.red)
            }

            Button {
                vm.resetSwitchCount()
            } label: {
                Label("Reset compteur", systemImage: "arrow.counterclockwise")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
        }
    }
}
