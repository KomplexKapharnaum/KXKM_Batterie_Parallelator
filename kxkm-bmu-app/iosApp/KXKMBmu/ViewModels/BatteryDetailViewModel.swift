import SwiftUI
import Shared

class BatteryDetailViewModel: ObservableObject {
    @Published var battery: BatteryState?
    @Published var history: [BatteryHistoryPoint] = []
    @Published var events: [AuditEvent] = []
    @Published var commandResult: String? = nil

    let batteryIndex: Int
    private let monitorUseCase: MonitoringUseCase
    private let controlUseCase: ControlUseCase

    init(batteryIndex: Int) {
        self.batteryIndex = batteryIndex
        self.monitorUseCase = SharedFactory.companion.createMonitoringUseCase()
        self.controlUseCase = SharedFactory.companion.createControlUseCase()
        startObserving()
        loadHistory()
    }

    private func startObserving() {
        monitorUseCase.observeBattery(index: Int32(batteryIndex)) { [weak self] state in
            DispatchQueue.main.async { self?.battery = state }
        }
    }

    private func loadHistory() {
        monitorUseCase.getHistory(batteryIndex: Int32(batteryIndex), hours: 24) { [weak self] points in
            DispatchQueue.main.async { self?.history = points }
        }
    }

    func switchBattery(on: Bool) {
        controlUseCase.switchBattery(index: Int32(batteryIndex), on: on) { [weak self] result in
            DispatchQueue.main.async {
                self?.commandResult = result.isSuccess ? "OK" : "Erreur: \(result.errorMessage ?? "")"
            }
        }
    }

    func resetSwitchCount() {
        controlUseCase.resetSwitchCount(index: Int32(batteryIndex)) { [weak self] result in
            DispatchQueue.main.async {
                self?.commandResult = result.isSuccess ? "Compteur remis à zéro" : "Erreur"
            }
        }
    }
}
