import SwiftUI
import Shared

class SohDashboardIOSViewModel: ObservableObject {
    @Published var scores: [MlScore] = []
    @Published var isRefreshing = false

    private let sohUseCase: SohUseCase

    init() {
        self.sohUseCase = AppFactory.shared.factory.sohUseCase
        startObserving()
        sohUseCase.start()
    }

    private func startObserving() {
        sohUseCase.observeMlScores { [weak self] scores in
            DispatchQueue.main.async { self?.scores = scores }
        }
    }

    func refresh() {
        guard !isRefreshing else { return }
        isRefreshing = true
        // observeMlScores déjà actif — le callback mettra à jour scores
        // On déclenche juste le refresh cloud
        sohUseCase.observeMlScores { [weak self] _ in
            DispatchQueue.main.async { self?.isRefreshing = false }
        }
    }
}
