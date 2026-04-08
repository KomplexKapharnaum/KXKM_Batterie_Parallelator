import SwiftUI

@MainActor
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
        Task {
            events = await auditUseCase.getEvents(
                action: filterAction,
                batteryIndex: filterBattery
            )
            pendingSyncCount = Int(auditUseCase.getPendingSyncCount())
        }
    }

    func clearFilters() {
        filterAction = nil
        filterBattery = nil
        reload()
    }
}
