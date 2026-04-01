import SwiftUI
// import Shared — using Stubs

struct AuditRowView: View {
    let event: AuditEvent

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: actionIcon)
                .foregroundColor(actionColor)
                .frame(width: 24)

            VStack(alignment: .leading, spacing: 2) {
                Text(event.action.replacingOccurrences(of: "_", with: " ").capitalized)
                    .font(.subheadline.bold())

                if let target = event.target?.intValue {
                    Text("Batterie \(target + 1)")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }

                if let detail = event.detail {
                    Text(detail)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }

            Spacer()

            VStack(alignment: .trailing, spacing: 2) {
                Text(event.userId)
                    .font(.caption2)
                    .foregroundColor(.secondary)
                Text(formatTimestamp(event.timestamp))
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
    }

    private var actionIcon: String {
        switch event.action {
        case "switch_on": return "bolt.fill"
        case "switch_off": return "bolt.slash"
        case "reset": return "arrow.counterclockwise"
        case "config_change": return "wrench"
        case "wifi_config": return "wifi"
        default: return "doc"
        }
    }

    private var actionColor: Color {
        switch event.action {
        case "switch_on": return .green
        case "switch_off": return .red
        case "config_change", "wifi_config": return .blue
        default: return .secondary
        }
    }

    private func formatTimestamp(_ ms: Int64) -> String {
        let date = Date(timeIntervalSince1970: Double(ms) / 1000.0)
        let fmt = DateFormatter()
        fmt.dateFormat = "dd/MM HH:mm:ss"
        return fmt.string(from: date)
    }
}
