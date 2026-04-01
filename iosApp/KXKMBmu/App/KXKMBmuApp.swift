import SwiftUI
// import Shared — using Stubs // KMP shared framework

@main
struct KXKMBmuApp: App {
    @StateObject private var authVM = AuthViewModel()

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
