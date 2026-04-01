import SwiftUI
import Combine

class BatteryDetailViewModel: ObservableObject {
    @Published var battery: BatteryState?
    @Published var history: [BatteryHistoryPoint] = []
    @Published var commandResult: String? = nil

    let batteryIndex: Int
    private let ble = BleManager.shared
    private var cancellables = Set<AnyCancellable>()

    init(batteryIndex: Int) {
        self.batteryIndex = batteryIndex

        ble.$batteries
            .receive(on: RunLoop.main)
            .map { $0.first { $0.index == batteryIndex } }
            .sink { [weak self] state in self?.battery = state }
            .store(in: &cancellables)

        ble.$lastCommandResult
            .receive(on: RunLoop.main)
            .compactMap { $0 }
            .sink { [weak self] r in
                self?.commandResult = r.isSuccess ? "OK" : (r.errorMessage ?? "Erreur")
            }
            .store(in: &cancellables)

        loadMockHistory()
    }

    func switchBattery(on: Bool) {
        ble.switchBattery(index: batteryIndex, on: on)
    }

    func resetSwitchCount() {
        ble.resetSwitchCount(index: batteryIndex)
    }

    private func loadMockHistory() {
        let now = Int64(Date().timeIntervalSince1970 * 1000)
        history = (0..<100).reversed().map { i in
            BatteryHistoryPoint(
                timestamp: now - Int64(i) * 360_000,
                voltageMv: 25500 + Int.random(in: -500...500),
                currentMa: 1200 + Int.random(in: -300...300)
            )
        }
    }
}
