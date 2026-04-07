import SwiftUI

struct VoltageChartView: View {
    let history: [BatteryHistoryPoint]

    var body: some View {
        if history.isEmpty {
            Text("Pas d'historique disponible")
                .foregroundColor(.secondary)
                .frame(height: 200)
        } else {
            // Simplified line chart (no Swift Charts dependency)
            GeometryReader { geo in
                let minV = Double(history.map(\.voltageMv).min() ?? 24000)
                let maxV = Double(history.map(\.voltageMv).max() ?? 30000)
                let range = max(maxV - minV, 1)

                Path { path in
                    for (i, point) in history.enumerated() {
                        let x = geo.size.width * CGFloat(i) / CGFloat(max(history.count - 1, 1))
                        let y = geo.size.height * (1 - CGFloat(Double(point.voltageMv) - minV) / CGFloat(range))
                        if i == 0 { path.move(to: CGPoint(x: x, y: y)) }
                        else { path.addLine(to: CGPoint(x: x, y: y)) }
                    }
                }
                .stroke(Color.blue, lineWidth: 2)
            }
            .frame(height: 200)
            .overlay(alignment: .topLeading) {
                Text(String(format: "%.2f V", Double(history.last?.voltageMv ?? 0) / 1000.0))
                    .font(.caption.bold())
                    .padding(4)
            }
        }
    }
}
