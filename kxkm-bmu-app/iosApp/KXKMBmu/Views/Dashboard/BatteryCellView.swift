import SwiftUI
import Shared

struct BatteryCellView: View {
    let battery: BatteryState

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("Bat \(battery.index + 1)")
                    .font(.caption.bold())
                Spacer()
                BatteryStateIcon(state: battery.state)
            }

            Text(battery.voltageMv.voltageDisplay)
                .font(.title3.monospacedDigit().bold())
                .foregroundColor(voltageColor)

            Text(battery.currentMa.currentDisplay)
                .font(.caption.monospacedDigit())
                .foregroundColor(.secondary)

            HStack {
                Image(systemName: "arrow.up.arrow.down")
                    .font(.caption2)
                Text("\(battery.nbSwitch)")
                    .font(.caption2)
            }
            .foregroundColor(.secondary)
        }
        .padding(10)
        .background(Color(.systemGray6))
        .cornerRadius(12)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(battery.state.color.opacity(0.5), lineWidth: 2)
        )
    }

    private var voltageColor: Color {
        let mv = battery.voltageMv
        if mv < 24000 || mv > 30000 { return .red }
        if mv < 24500 || mv > 29500 { return .orange }
        return .primary
    }
}
