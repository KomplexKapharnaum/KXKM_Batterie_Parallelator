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
    private let mockUseCase = MonitoringUseCase()
    private var observeTasks: [Task<Void, Never>] = []

    init() {
        observeTasks.append(Task { [weak self] in
            for await connected in BleManager.shared.$isConnected.values {
                self?.isBleConnected = connected
            }
        })

        observeTasks.append(Task { [weak self] in
            for await bleBatteries in BleManager.shared.$batteries.values {
                guard let self else { return }
                if !bleBatteries.isEmpty {
                    self.batteries = bleBatteries
                    self.isLoading = false
                }
            }
        })

        // Fallback: use mock data if BLE not connected after 3s
        observeTasks.append(Task { [weak self] in
            try? await Task.sleep(nanoseconds: 3_000_000_000)
            guard let self, self.batteries.isEmpty else { return }
            let mockBatteries = await self.mockUseCase.batteries()
            if self.ble.batteries.isEmpty {
                self.batteries = mockBatteries
                self.isLoading = false
            }
        })

        ble.startScan()
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
