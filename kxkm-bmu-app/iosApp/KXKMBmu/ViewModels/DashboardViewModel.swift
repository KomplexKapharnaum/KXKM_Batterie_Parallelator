import SwiftUI
import Shared
import Combine

class DashboardViewModel: ObservableObject {
    @Published var batteries: [BatteryState] = []
    @Published var isLoading = true

    private let monitorUseCase: MonitoringUseCase

    init() {
        self.monitorUseCase = SharedFactory.companion.createMonitoringUseCase()
        startObserving()
    }

    private func startObserving() {
        // Collect KMP StateFlow<List<BatteryState>> into @Published
        // Exact bridge depends on Plan 2 (SKIE or manual FlowCollector)
        monitorUseCase.observeBatteries { [weak self] states in
            DispatchQueue.main.async {
                self?.batteries = states
                self?.isLoading = false
            }
        }
    }
}
