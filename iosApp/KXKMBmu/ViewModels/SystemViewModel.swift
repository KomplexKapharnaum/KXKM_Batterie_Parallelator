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
                guard let self else { return }
                self.system = info
            }
        })

        observeTasks.append(Task { [weak self] in
            for await data in BleManager.shared.$solarData.values {
                self?.solar = data
            }
        })
    }

    deinit {
        observeTasks.forEach { $0.cancel() }
    }
}
