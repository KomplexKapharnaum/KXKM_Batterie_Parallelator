import SwiftUI
import Combine

class SystemViewModel: ObservableObject {
    @Published var system: SystemInfo? = nil
    @Published var solar: SolarData? = nil

    private let ble = BleManager.shared
    private var cancellables = Set<AnyCancellable>()

    init() {
        ble.$systemInfo
            .receive(on: RunLoop.main)
            .sink { [weak self] info in self?.system = info }
            .store(in: &cancellables)

        ble.$solarData
            .receive(on: RunLoop.main)
            .sink { [weak self] data in self?.solar = data }
            .store(in: &cancellables)

        // Fallback mock if no BLE after 3s
        DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in
            guard let self, self.system == nil else { return }
            self.system = SystemInfo(
                firmwareVersion: "0.5.0 (mock)", heapFree: 16800000,
                uptimeSeconds: 0, wifiIp: nil,
                nbIna: 0, nbTca: 0, topologyValid: false
            )
        }
    }
}
