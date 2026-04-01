import SwiftUI

struct ProtectionConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        Form {
            Section("Seuils de tension") {
                Stepper("V min: \(vm.minMv) mV", value: $vm.minMv, in: 20000...30000, step: 500)
                Stepper("V max: \(vm.maxMv) mV", value: $vm.maxMv, in: 25000...35000, step: 500)
                Stepper("V diff max: \(vm.diffMv) mV", value: $vm.diffMv, in: 100...5000, step: 100)
            }
            Section("Seuil de courant") {
                Stepper("I max: \(vm.maxMa) mA", value: $vm.maxMa, in: 1000...50000, step: 1000)
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
