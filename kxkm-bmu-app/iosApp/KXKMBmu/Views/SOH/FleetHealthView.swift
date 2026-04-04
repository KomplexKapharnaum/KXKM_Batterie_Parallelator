import SwiftUI
import Shared

struct FleetHealthView: View {
    @StateObject private var viewModel = FleetIOSViewModel()

    var body: some View {
        NavigationView {
            VStack(spacing: 24) {
                if let fleet = viewModel.fleetHealth {
                    SohGaugeView(sohPercent: Int(fleet.fleetHealth * 100), size: 160)

                    Text(String(format: "D\u00e9s\u00e9quilibre: %.0f%%", fleet.imbalanceSeverity * 100))
                        .foregroundColor(fleet.imbalanceSeverity > 0.5 ? .red : fleet.imbalanceSeverity > 0.2 ? .orange : .secondary)

                    if fleet.outlierScore > 0.3 {
                        HStack {
                            Image(systemName: "exclamationmark.triangle.fill")
                                .foregroundColor(fleet.outlierScore > 0.7 ? .red : .orange)
                            VStack(alignment: .leading) {
                                Text("Batterie \(fleet.outlierIdx + 1) \u2014 anomalie")
                                    .font(.headline)
                                Text(String(format: "Score: %.0f%%", fleet.outlierScore * 100))
                                    .font(.caption)
                                    .foregroundColor(.secondary)
                            }
                        }
                        .padding()
                        .background(Color.orange.opacity(0.1))
                        .cornerRadius(12)
                    }
                } else {
                    Text("Donn\u00e9es flotte non disponibles")
                        .foregroundColor(.secondary)
                }

                Spacer()
            }
            .padding()
            .navigationTitle("Vue Flotte")
        }
    }
}
