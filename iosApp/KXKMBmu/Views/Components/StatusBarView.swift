import SwiftUI

struct StatusBarView: View {
    @StateObject private var transport = TransportStatusViewModel()

    private let ble = BleManager.shared

    var body: some View {
        HStack {
            Image(systemName: transport.channel.icon)
                .foregroundColor(transport.isConnected ? .green : .orange)
            Text(transport.channel.displayName)
                .font(.caption.bold())

            // BLE scanning indicator
            if ble.isScanning {
                ProgressView()
                    .scaleEffect(0.7)
                    .padding(.leading, 4)
                Text("Recherche BMU...")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }

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

@MainActor
class TransportStatusViewModel: ObservableObject {
    @Published var channel: TransportChannel = .offline
    @Published var isConnected = false
    @Published var deviceName: String? = nil
    @Published var rssi: Int? = nil

    private let ble = BleManager.shared
    private var observeTasks: [Task<Void, Never>] = []

    init() {
        observeTasks.append(Task { [weak self] in
            for await connected in BleManager.shared.$isConnected.values {
                guard let self else { return }
                self.isConnected = connected
                self.channel = connected ? .ble : .offline
                if !connected {
                    self.deviceName = nil
                    self.rssi = nil
                }
            }
        })

        observeTasks.append(Task { [weak self] in
            for await name in BleManager.shared.$connectedDeviceName.values {
                guard let self else { return }
                self.deviceName = name
            }
        })

        observeTasks.append(Task { [weak self] in
            for await r in BleManager.shared.$rssi.values {
                guard let self else { return }
                self.rssi = r != 0 ? r : nil
            }
        })
    }

    deinit {
        observeTasks.forEach { $0.cancel() }
    }
}
