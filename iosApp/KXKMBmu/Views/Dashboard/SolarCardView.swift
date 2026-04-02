import SwiftUI

struct SolarCardView: View {
    let solar: SolarData

    private var chargeStateName: String {
        switch solar.chargeState {
        case 0: return "OFF"
        case 3: return "BULK"
        case 4: return "ABSORPTION"
        case 5: return "FLOAT"
        case 7: return "EQUALIZE"
        default: return "?"
        }
    }

    private var chargeStateColor: Color {
        switch solar.chargeState {
        case 0: return .gray
        case 3: return .orange
        case 4: return .yellow
        case 5: return .green
        case 7: return .blue
        default: return .gray
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Image(systemName: "sun.max.fill")
                    .foregroundColor(.yellow)
                Text("Solar")
                    .font(.headline)
                    .foregroundColor(.white)
                Spacer()
                Text(chargeStateName)
                    .font(.caption)
                    .fontWeight(.bold)
                    .foregroundColor(chargeStateColor)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 2)
                    .background(chargeStateColor.opacity(0.2))
                    .cornerRadius(4)
            }

            HStack(spacing: 16) {
                VStack(alignment: .leading, spacing: 2) {
                    Text("PV")
                        .font(.caption2)
                        .foregroundColor(.gray)
                    Text("\(solar.panelPowerW) W")
                        .font(.title3)
                        .fontWeight(.bold)
                        .foregroundColor(.yellow)
                        .monospacedDigit()
                }

                VStack(alignment: .leading, spacing: 2) {
                    Text("V PV")
                        .font(.caption2)
                        .foregroundColor(.gray)
                    Text(String(format: "%.1f V", Double(solar.panelVoltageMv) / 1000.0))
                        .font(.callout)
                        .foregroundColor(.cyan)
                        .monospacedDigit()
                }

                VStack(alignment: .leading, spacing: 2) {
                    Text("Yield")
                        .font(.caption2)
                        .foregroundColor(.gray)
                    Text("\(solar.yieldTodayWh) Wh")
                        .font(.callout)
                        .foregroundColor(.green)
                        .monospacedDigit()
                }

                Spacer()
            }
        }
        .padding(12)
        .background(Color(white: 0.1))
        .cornerRadius(12)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(chargeStateColor.opacity(0.3), lineWidth: 1)
        )
    }
}
