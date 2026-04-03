import SwiftUI
import Combine

class DashboardViewModel: ObservableObject {
    @Published var batteries: [BatteryState] = []
    @Published var isLoading = true
    @Published var isBleConnected = false

    var solarData: SolarData? {
        ble.solarData
    }

    private let ble = BleManager.shared
    private let mockUseCase = MonitoringUseCase()
    private var cancellables = Set<AnyCancellable>()

    init() {
        // Observe BLE connection state
        ble.$isConnected
            .receive(on: RunLoop.main)
            .sink { [weak self] connected in
                self?.isBleConnected = connected
            }
            .store(in: &cancellables)

        // Observe BLE batteries (real data)
        ble.$batteries
            .receive(on: RunLoop.main)
            .sink { [weak self] bleBatteries in
                guard let self else { return }
                if !bleBatteries.isEmpty {
                    self.batteries = bleBatteries
                    self.isLoading = false
                }
            }
            .store(in: &cancellables)

        // Fallback: use mock data if BLE not connected after 3s
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in
            guard let self, self.batteries.isEmpty else { return }
            self.mockUseCase.observeBatteries { [weak self] states in
                DispatchQueue.main.async {
                    guard let self, self.ble.batteries.isEmpty else { return }
                    self.batteries = states
                    self.isLoading = false
                }
            }
        }

        // Start BLE scan
        ble.startScan()
    }

    /// Pull-to-refresh: restart BLE scan to get fresh data
    func refresh() async {
        await MainActor.run { isLoading = true }
        ble.startScan()
        // Allow a brief scan window before resolving
        try? await Task.sleep(nanoseconds: 2_000_000_000)
        await MainActor.run { isLoading = false }
    }
}
