import SwiftUI

struct BatteryLabelsView: View {
    @State private var editingIndex: Int? = nil
    @State private var editText: String = ""
    private let bleManager = BleManager.shared

    var body: some View {
        List {
            let nb = bleManager.systemInfo?.nbIna ?? 4
            ForEach(0..<nb, id: \.self) { i in
                HStack {
                    Text("Bat \(i + 1)")
                        .foregroundColor(.secondary)
                    Spacer()
                    Text(bleManager.batteryLabels[i] ?? "B\(i+1)")
                        .font(.body.bold())
                    Button(action: {
                        editingIndex = i
                        editText = bleManager.batteryLabels[i] ?? ""
                    }) {
                        Image(systemName: "pencil")
                    }
                }
            }
        }
        .navigationTitle("Noms batteries")
        .alert("Renommer", isPresented: Binding(
            get: { editingIndex != nil },
            set: { if !$0 { editingIndex = nil } }
        )) {
            TextField("Nom (max 8 car.)", text: $editText)
            Button("Envoyer") {
                if let idx = editingIndex {
                    bleManager.setBatteryLabel(index: idx, label: String(editText.prefix(8)))
                }
                editingIndex = nil
            }
            Button("Annuler", role: .cancel) { editingIndex = nil }
        }
    }
}
