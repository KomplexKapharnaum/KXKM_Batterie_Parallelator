import SwiftUI
import Charts
import Shared

struct VoltageChartView: View {
    let history: [BatteryHistoryPoint]

    var body: some View {
        if history.isEmpty {
            Text("Pas d'historique disponible")
                .foregroundColor(.secondary)
                .frame(height: 200)
        } else {
            Chart {
                ForEach(history, id: \.timestamp) { point in
                    LineMark(
                        x: .value("Temps", Date(timeIntervalSince1970: Double(point.timestamp) / 1000.0)),
                        y: .value("Tension (V)", Double(point.voltageMv) / 1000.0)
                    )
                    .foregroundStyle(.blue)
                }
            }
            .chartYScale(domain: 22...32)
            .chartYAxisLabel("Tension (V)")
            .chartXAxis {
                AxisMarks(values: .stride(by: .hour, count: 2)) {
                    AxisValueLabel(format: .dateTime.hour())
                }
            }
            .frame(height: 200)
        }
    }
}
