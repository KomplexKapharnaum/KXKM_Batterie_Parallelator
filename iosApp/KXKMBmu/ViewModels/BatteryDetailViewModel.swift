import SwiftUI

@MainActor
class BatteryDetailViewModel: ObservableObject {
    @Published var battery: BatteryState?
    @Published var history: [BatteryHistoryPoint] = []
    @Published var commandResult: String? = nil

    let batteryIndex: Int
    private let ble = BleManager.shared
    private var observeTasks: [Task<Void, Never>] = []

    init(batteryIndex: Int) {
        self.batteryIndex = batteryIndex

        observeTasks.append(Task { [weak self] in
            guard let self else { return }
            for await bleBatteries in BleManager.shared.$batteries.values {
                self.battery = bleBatteries.first { $0.index == batteryIndex }
            }
        })

        observeTasks.append(Task { [weak self] in
            for await result in BleManager.shared.$lastCommandResult.values {
                guard let self, let r = result else { continue }
                self.commandResult = r.isSuccess ? "OK" : (r.errorMessage ?? "Erreur")
            }
        })

        loadMockHistory()
    }

    deinit {
        observeTasks.forEach { $0.cancel() }
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
