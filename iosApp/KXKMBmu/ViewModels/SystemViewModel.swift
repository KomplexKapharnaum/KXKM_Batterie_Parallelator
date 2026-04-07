import SwiftUI

@MainActor
class SystemViewModel: ObservableObject {
    @Published var system: SystemInfo? = nil
    @Published var solar: SolarData? = nil

    private let ble = BleManager.shared
    private var observeTasks: [Task<Void, Never>] = []
    private var fallbackTask: Task<Void, Never>?

    init() {
        observeTasks.append(Task { [weak self] in
            for await info in BleManager.shared.$systemInfo.values {
                guard let self else { return }
                self.system = info
                if info != nil {
                    self.fallbackTask?.cancel()
                }
            }
        })

        observeTasks.append(Task { [weak self] in
            for await data in BleManager.shared.$solarData.values {
                self?.solar = data
            }
        })

        // Fallback mock if no BLE data after 8s
        fallbackTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: 8_000_000_000)
            guard let self, self.system == nil else { return }
            self.system = SystemInfo(
                firmwareVersion: "0.5.0 (mock)", heapFree: 16800000,
                uptimeSeconds: 0, wifiIp: nil,
                nbIna: 0, nbTca: 0, topologyValid: false
            )
        }
        observeTasks.append(fallbackTask!)
    }

    deinit {
        observeTasks.forEach { $0.cancel() }
    }
}
