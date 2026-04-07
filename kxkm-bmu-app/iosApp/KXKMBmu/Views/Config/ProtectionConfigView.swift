import SwiftUI

struct ProtectionConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    private func fmtV(_ mv: Int) -> String { String(format: "%.3fV", Double(mv) / 1000.0) }
    private func fmtA(_ ma: Int) -> String { String(format: "%.3fA", Double(ma) / 1000.0) }

    var body: some View {
        Form {
            Section("Seuils de tension") {
                Stepper("V min: \(fmtV(vm.minMv))", value: $vm.minMv, in: 20000...30000, step: 25)
                Stepper("V max: \(fmtV(vm.maxMv))", value: $vm.maxMv, in: 25000...35000, step: 25)
                Stepper("V diff max: \(fmtV(vm.diffMv))", value: $vm.diffMv, in: 100...5000, step: 25)
            }
            Section("Seuil de courant") {
                Stepper("I max: \(fmtA(vm.maxMa))", value: $vm.maxMa, in: 1000...50000, step: 25)
            }
            Section {
                Button("Envoyer au BMU") { vm.saveProtection() }
                    .buttonStyle(.borderedProminent)
            }
            if let msg = vm.statusMessage {
                Section { Text(msg).foregroundColor(.green) }
            }
        }
        .navigationTitle("Protection")
    }
}
