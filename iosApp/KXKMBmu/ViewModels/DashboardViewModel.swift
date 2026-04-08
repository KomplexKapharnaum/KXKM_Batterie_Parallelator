import SwiftUI

@MainActor
class DashboardViewModel: ObservableObject {
    @Published var batteries: [BatteryState] = []
    @Published var isLoading = true
    @Published var isBleConnected = false

    var solarData: SolarData? {
        ble.solarData
    }

    private let ble = BleManager.shared
    private var observeTasks: [Task<Void, Never>] = []

    init() {
        observeTasks.append(Task { [weak self] in
            for await connected in BleManager.shared.$isConnected.values {
                guard let self else { return }
                self.isBleConnected = connected
                if !connected {
                    self.batteries = []
                    self.isLoading = true
                }
            }
        })

        observeTasks.append(Task { [weak self] in
            for await bleBatteries in BleManager.shared.$batteries.values {
                guard let self else { return }
                self.batteries = bleBatteries
                if !bleBatteries.isEmpty {
                    self.isLoading = false
                }
            }
        })

        // Stop spinner once topology is received (even if 0 batteries)
        observeTasks.append(Task { [weak self] in
            for await received in BleManager.shared.$topologyReceived.values {
                guard let self else { return }
                if received {
                    self.isLoading = false
                }
            }
        })
    }

    deinit {
        observeTasks.forEach { $0.cancel() }
    }

    /// Pull-to-refresh: restart BLE scan to get fresh data
    func refresh() async {
        isLoading = true
        ble.startScan()
        try? await Task.sleep(nanoseconds: 2_000_000_000)
        isLoading = false
    }
}
