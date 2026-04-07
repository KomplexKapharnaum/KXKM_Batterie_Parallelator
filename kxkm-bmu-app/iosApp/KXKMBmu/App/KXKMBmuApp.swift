import SwiftUI
import Shared

@main
struct KXKMBmuApp: App {
    @StateObject private var authVM = AuthViewModel()
    private let sohNotifications = SohNotificationDelegate()

    init() {
        sohNotifications.requestPermission()
        let sohUseCase = AppFactory.shared.factory.sohUseCase
        sohUseCase.start()
        sohUseCase.observeMlScores { [sohNotifications] scores in
            DispatchQueue.main.async {
                sohNotifications.checkAndNotify(scores: scores)
            }
        }
    }

    var body: some Scene {
        WindowGroup {
            if authVM.isAuthenticated {
                ContentView()
                    .environmentObject(authVM)
            } else if authVM.needsOnboarding {
                OnboardingView()
                    .environmentObject(authVM)
            } else {
                PinEntryView()
                    .environmentObject(authVM)
            }
        }
    }
}
