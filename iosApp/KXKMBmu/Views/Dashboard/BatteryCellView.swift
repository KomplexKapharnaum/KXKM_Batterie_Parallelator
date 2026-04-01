import SwiftUI
// import Shared — using Stubs

struct BatteryCellView: View {
    let battery: BatteryState

    private var stateColor: Color {
        battery.state.color
    }

    var body: some View {
        HStack(spacing: 0) {
            // Colored left border strip
            RoundedRectangle(cornerRadius: 2)
                .fill(stateColor)
                .frame(width: 4)
                .padding(.vertical, 4)

            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text("Bat \(battery.index + 1)")
                        .font(.caption.bold())
                        .foregroundColor(.secondary)
                    Spacer()
                    BatteryStateIcon(state: battery.state)
                }

                Text(battery.voltageMv.voltageDisplay)
                    .font(.system(.title2, design: .monospaced).bold())
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
            .padding(.leading, 8)
            .padding(.trailing, 10)
            .padding(.vertical, 10)
        }
        .background(
            LinearGradient(
                colors: [
                    Color(.systemGray6),
                    Color(.systemGray6).opacity(0.7)
                ],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
        )
        .cornerRadius(12)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(stateColor.opacity(0.3), lineWidth: 1)
        )
    }

    private var voltageColor: Color {
        let mv = battery.voltageMv
        if mv < 24000 || mv > 30000 { return .red }
        if mv < 24500 || mv > 29500 { return .orange }
        return .primary
    }
}
