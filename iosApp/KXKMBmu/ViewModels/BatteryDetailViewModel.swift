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
            for await bleBatteries in BleManager.shared.$batteries.values {
                guard let self else { return }
                self.battery = bleBatteries.first { $0.index == batteryIndex }
            }
        })

        observeTasks.append(Task { [weak self] in
            for await result in BleManager.shared.$lastCommandResult.values {
                guard let self else { return }
                guard let r = result else { continue }
                self.commandResult = r.isSuccess ? "OK" : (r.errorMessage ?? "Erreur")
            }
        })

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
}
