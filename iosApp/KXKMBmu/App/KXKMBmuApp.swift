import SwiftUI

@main
struct KXKMBmuApp: App {
    @StateObject private var authVM: AuthViewModel = {
        let vm = AuthViewModel()
        #if DEBUG
        // Auto-login admin pour dev — skip onboarding/PIN
        vm.isAuthenticated = true
        vm.currentUser = UserProfile(id: "admin", name: "Admin", role: .admin, pinHash: "", salt: "")
        #endif
        return vm
    }()

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(authVM)
        }
    }
}
