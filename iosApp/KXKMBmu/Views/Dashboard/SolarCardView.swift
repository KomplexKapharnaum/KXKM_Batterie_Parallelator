import SwiftUI

struct SolarCardView: View {
    let solar: SolarData

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Image(systemName: "sun.max.fill")
                    .foregroundColor(.yellow)
                Text("Solar")
                    .font(.headline)
                    .foregroundColor(.white)
                Spacer()
                Text(solar.chargeStateName)
                    .font(.caption)
                    .fontWeight(.bold)
                    .foregroundColor(solar.chargeStateColor)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 2)
                    .background(solar.chargeStateColor.opacity(0.2))
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
                .stroke(solar.chargeStateColor.opacity(0.3), lineWidth: 1)
        )
    }
}
