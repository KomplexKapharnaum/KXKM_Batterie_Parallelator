import SwiftUI

@MainActor
class SystemViewModel: ObservableObject {
    @Published var system: SystemInfo? = nil
    @Published var solar: SolarData? = nil

    private let ble = BleManager.shared
    private var observeTasks: [Task<Void, Never>] = []

    init() {
        observeTasks.append(Task { [weak self] in
            for await info in BleManager.shared.$systemInfo.values {
                self?.system = info
            }
        })

        observeTasks.append(Task { [weak self] in
            for await data in BleManager.shared.$solarData.values {
                self?.solar = data
            }
        })

        // Fallback mock if no BLE after 3s
        observeTasks.append(Task { [weak self] in
            try? await Task.sleep(nanoseconds: 3_000_000_000)
            guard let self, self.system == nil else { return }
            self.system = SystemInfo(
                firmwareVersion: "0.5.0 (mock)", heapFree: 16800000,
                uptimeSeconds: 0, wifiIp: nil,
                nbIna: 0, nbTca: 0, topologyValid: false
            )
        })
    }

    deinit {
        observeTasks.forEach { $0.cancel() }
    }
}
