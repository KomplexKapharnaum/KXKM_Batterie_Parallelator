import SwiftUI
import Shared
import Combine

class AuthViewModel: ObservableObject {
    @Published var isAuthenticated = false
    @Published var needsOnboarding = false
    @Published var currentUser: UserProfile? = nil
    @Published var pinError: String? = nil

    private let authUseCase: AuthUseCase // from KMP shared

    init() {
        self.authUseCase = SharedFactory.companion.createAuthUseCase()
        self.needsOnboarding = authUseCase.hasNoUsers()
    }

    func login(pin: String) {
        let result = authUseCase.authenticate(pin: pin)
        if let user = result {
            currentUser = user
            isAuthenticated = true
            pinError = nil
        } else {
            pinError = "PIN incorrect"
        }
    }

    func createAdmin(name: String, pin: String) {
        authUseCase.createUser(name: name, pin: pin, role: .admin)
        needsOnboarding = false
    }

    func logout() {
        isAuthenticated = false
        currentUser = nil
    }
}
