import SwiftUI
import Shared

struct StatusBarView: View {
    @StateObject private var transport = TransportStatusViewModel()

    var body: some View {
        HStack {
            Image(systemName: transport.channel.icon)
                .foregroundColor(transport.isConnected ? .green : .orange)
            Text(transport.channel.displayName)
                .font(.caption.bold())

            Spacer()

            if let deviceName = transport.deviceName {
                Text(deviceName)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }

            if let rssi = transport.rssi {
                Image(systemName: rssiIcon(rssi))
                    .foregroundColor(.secondary)
                Text("\(rssi) dBm")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.horizontal)
        .padding(.vertical, 6)
        .background(Color(.systemGray6))
    }

    private func rssiIcon(_ rssi: Int) -> String {
        if rssi > -50 { return "wifi" }
        if rssi > -70 { return "wifi" }
        return "wifi.exclamationmark"
    }
}

class TransportStatusViewModel: ObservableObject {
    @Published var channel: TransportChannel = .offline
    @Published var isConnected = false
    @Published var deviceName: String? = nil
    @Published var rssi: Int? = nil

    private var observeTask: Task<Void, Never>?

    init() {
        let manager = AppFactory.shared.factory.transportManager
        observeTask = Task { @MainActor [weak self] in
            for await ch in manager.activeChannel {
                self?.channel = ch
                self?.isConnected = (ch != .offline)
            }
        }
    }

    deinit { observeTask?.cancel() }
}
