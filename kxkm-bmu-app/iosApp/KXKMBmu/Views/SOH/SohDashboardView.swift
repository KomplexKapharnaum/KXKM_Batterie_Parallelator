import SwiftUI
import Shared

struct SohDashboardView: View {
    @StateObject private var viewModel = SohDashboardIOSViewModel()

    let columns = [GridItem(.flexible()), GridItem(.flexible())]

    var body: some View {
        NavigationView {
            ScrollView {
                if viewModel.scores.isEmpty {
                    Text("Aucune donn\u00e9e SOH disponible")
                        .foregroundColor(.secondary)
                        .padding(.top, 40)
                } else {
                    LazyVGrid(columns: columns, spacing: 12) {
                        ForEach(viewModel.scores, id: \.battery) { score in
                            SohBatteryCardView(score: score)
                        }
                    }
                    .padding()
                }
            }
            .navigationTitle("SOH Batteries")
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button(action: { viewModel.refresh() }) {
                        Image(systemName: "arrow.clockwise")
                    }
                }
            }
        }
    }
}

struct SohBatteryCardView: View {
    let score: MlScore

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("Bat \(score.battery + 1)")
                    .font(.caption)
                Spacer()
                SohGaugeView(sohPercent: Int(score.sohScore * 100), size: 48)
            }
            Text("RUL: \(score.rulDays)j")
                .font(.caption2).monospacedDigit()
            Text(String(format: "Anomalie: %.0f%%", score.anomalyScore * 100))
                .font(.caption2)
                .foregroundColor(score.anomalyScore > 0.7 ? .red : score.anomalyScore > 0.3 ? .orange : .secondary)
        }
        .padding(12)
        .background(Color(.systemGray6))
        .cornerRadius(12)
    }
}
