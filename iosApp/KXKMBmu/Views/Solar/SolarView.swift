import SwiftUI

struct SolarView: View {
    @ObservedObject private var ble = BleManager.shared

    private var solar: SolarData? { ble.solarData }

    var body: some View {
        NavigationStack {
            Group {
                if let s = solar {
                    ScrollView {
                        VStack(spacing: 16) {
                            solarContent(s)
                        }
                        .padding()
                    }
                } else {
                    VStack(spacing: 12) {
                        Image(systemName: "sun.max.trianglebadge.exclamationmark")
                            .font(.system(size: 48))
                            .foregroundColor(.gray)
                        Text("Pas de chargeur solaire")
                            .foregroundColor(.secondary)
                        Text("Connectez un SmartSolar\nvia VE.Direct")
                            .font(.caption)
                            .foregroundColor(.gray)
                            .multilineTextAlignment(.center)
                    }
                }
            }
            .navigationTitle("Solar")
        }
    }

    @ViewBuilder
    private func solarContent(_ s: SolarData) -> some View {
        HStack {
            Image(systemName: "sun.max.fill")
                .foregroundColor(.yellow)
                .font(.title2)
            Text("SmartSolar MPPT")
                .font(.headline)
                .foregroundColor(.white)
            Spacer()
            Text(s.chargeStateName)
                .font(.subheadline)
                .fontWeight(.bold)
                .foregroundColor(s.chargeStateColor)
                .padding(.horizontal, 12)
                .padding(.vertical, 4)
                .background(s.chargeStateColor.opacity(0.2))
                .cornerRadius(8)
        }
        .padding()
        .background(Color(white: 0.1))
        .cornerRadius(12)

        VStack(spacing: 8) {
            Text("Puissance PV")
                .font(.caption)
                .foregroundColor(.gray)
            Text("\(s.panelPowerW)")
                .font(.system(size: 56, weight: .bold, design: .rounded))
                .foregroundColor(.yellow)
                .monospacedDigit()
            Text("watts")
                .font(.caption)
                .foregroundColor(.gray)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 24)
        .background(Color(white: 0.08))
        .cornerRadius(16)

        LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], spacing: 12) {
            solarStat(
                title: "PV Tension",
                value: String(format: "%.1f V", Double(s.panelVoltageMv) / 1000.0),
                icon: "bolt.fill",
                color: .cyan
            )
            solarStat(
                title: "Batterie MPPT",
                value: String(format: "%.2f V", Double(s.batteryVoltageMv) / 1000.0),
                icon: "battery.75percent",
                color: .green
            )
            solarStat(
                title: "Courant charge",
                value: String(format: "%.1f A", Double(s.batteryCurrentMa) / 1000.0),
                icon: "arrow.down.circle.fill",
                color: .mint
            )
            solarStat(
                title: "Yield aujourd'hui",
                value: "\(s.yieldTodayWh) Wh",
                icon: "chart.bar.fill",
                color: .orange
            )
        }

        VStack(alignment: .leading, spacing: 8) {
            Text("Historique production")
                .font(.caption)
                .foregroundColor(.gray)
            HStack {
                Spacer()
                VStack(spacing: 6) {
                    Image(systemName: "chart.line.uptrend.xyaxis")
                        .font(.title)
                        .foregroundColor(.gray)
                    Text("Graphique à venir")
                        .font(.caption2)
                        .foregroundColor(.gray)
                }
                Spacer()
            }
            .padding(.vertical, 20)
        }
        .padding(12)
        .background(Color(white: 0.08))
        .cornerRadius(12)
    }

    private func solarStat(title: String, value: String, icon: String, color: Color) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Image(systemName: icon)
                    .foregroundColor(color)
                    .font(.caption)
                Text(title)
                    .font(.caption)
                    .foregroundColor(.gray)
            }
            Text(value)
                .font(.title3)
                .fontWeight(.semibold)
                .foregroundColor(.white)
                .monospacedDigit()
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .background(Color(white: 0.1))
        .cornerRadius(10)
    }
}
