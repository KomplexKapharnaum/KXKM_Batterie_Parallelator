import SwiftUI
import Shared

class FleetIOSViewModel: ObservableObject {
    @Published var fleetHealth: FleetHealth? = nil

    private let sohUseCase: SohUseCase

    init() {
        self.sohUseCase = AppFactory.shared.factory.sohUseCase
        startObserving()
    }

    private func startObserving() {
        sohUseCase.observeFleetHealth { [weak self] health in
            DispatchQueue.main.async { self?.fleetHealth = health }
        }
    }
}
