import SwiftUI
// import Shared — using Stubs // KMP shared framework

@main
struct KXKMBmuApp: App {
    @StateObject private var authVM: AuthViewModel = {
        let vm = AuthViewModel()
        // Auto-login admin pour dev — skip onboarding/PIN
        vm.isAuthenticated = true
        vm.currentUser = UserProfile(id: "admin", name: "Admin", role: .admin, pinHash: "", salt: "")
        return vm
    }()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(authVM)
        }
    }
}
