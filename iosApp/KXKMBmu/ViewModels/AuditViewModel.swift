import SwiftUI
// import Shared — using Stubs

class AuditViewModel: ObservableObject {
    @Published var events: [AuditEvent] = []
    @Published var filterAction: String? = nil
    @Published var filterBattery: Int? = nil
    @Published var pendingSyncCount: Int = 0

    private let auditUseCase: AuditUseCase

    init() {
        self.auditUseCase = SharedFactory.companion.createAuditUseCase()
        reload()
    }

    func reload() {
        auditUseCase.getEvents(
            action: filterAction,
            batteryIndex: filterBattery.map { Int32($0) }
        ) { [weak self] result in
            DispatchQueue.main.async {
                self?.events = result
            }
        }
        pendingSyncCount = Int(auditUseCase.getPendingSyncCount())
    }

    func clearFilters() {
        filterAction = nil
        filterBattery = nil
        reload()
    }
}
