import SwiftUI
import Shared

struct DiagnosticCardView: View {
    let diagnostic: Diagnostic?
    let isLoading: Bool
    let onRefresh: () -> Void

    private var bgColor: Color {
        switch diagnostic?.severity {
        case .critical: return Color.red.opacity(0.1)
        case .warning: return Color.orange.opacity(0.1)
        default: return Color.green.opacity(0.1)
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Image(systemName: diagnostic?.severity == .info ? "info.circle" : "exclamationmark.triangle")
                    .foregroundColor(diagnostic?.severity == .critical ? .red : diagnostic?.severity == .warning ? .orange : .green)
                Text("Diagnostic IA")
                    .font(.headline)
                Spacer()
                Button(action: onRefresh) {
                    if isLoading {
                        ProgressView()
                    } else {
                        Image(systemName: "arrow.clockwise")
                    }
                }
                .disabled(isLoading)
            }

            if let diag = diagnostic {
                Text(diag.diagnostic)
                    .font(.body)
                let date = Date(timeIntervalSince1970: TimeInterval(diag.generatedAt))
                Text("G\u00e9n\u00e9r\u00e9 le \(date.formatted(date: .abbreviated, time: .shortened))")
                    .font(.caption)
                    .foregroundColor(.secondary)
            } else {
                Text("Aucun diagnostic disponible.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .padding()
        .background(bgColor)
        .cornerRadius(12)
    }
}
