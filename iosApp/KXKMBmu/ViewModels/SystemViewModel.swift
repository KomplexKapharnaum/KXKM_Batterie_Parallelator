import SwiftUI
// import Shared — using Stubs

class SystemViewModel: ObservableObject {
    @Published var system: SystemInfo? = nil
    @Published var solar: SolarData? = nil

    private let monitorUseCase: MonitoringUseCase

    init() {
        self.monitorUseCase = SharedFactory.companion.createMonitoringUseCase()
        startObserving()
    }

    private func startObserving() {
        monitorUseCase.observeSystem { [weak self] info in
            DispatchQueue.main.async { self?.system = info }
        }
        monitorUseCase.observeSolar { [weak self] data in
            DispatchQueue.main.async { self?.solar = data }
        }
    }
}
