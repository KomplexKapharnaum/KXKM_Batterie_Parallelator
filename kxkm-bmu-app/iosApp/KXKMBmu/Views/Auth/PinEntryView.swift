import SwiftUI

struct PinEntryView: View {
    @EnvironmentObject var authVM: AuthViewModel
    @State private var pin = ""
    @State private var showBiometric = true

    var body: some View {
        VStack(spacing: 32) {
            Spacer()

            Image(systemName: "battery.100.bolt")
                .font(.system(size: 60))
                .foregroundColor(.green)

            Text("KXKM BMU")
                .font(.title.bold())

            Text("Entrez votre PIN")
                .font(.subheadline)
                .foregroundColor(.secondary)

            // PIN dots
            HStack(spacing: 16) {
                ForEach(0..<6, id: \.self) { i in
                    Circle()
                        .fill(i < pin.count ? Color.primary : Color.gray.opacity(0.3))
                        .frame(width: 16, height: 16)
                }
            }

            if let error = authVM.pinError {
                Text(error)
                    .foregroundColor(.red)
                    .font(.caption)
            }

            // Number pad
            LazyVGrid(columns: Array(repeating: GridItem(.flexible()), count: 3), spacing: 16) {
                ForEach(1...9, id: \.self) { num in
                    PinButton(label: "\(num)") { pin.append("\(num)") }
                }
                PinButton(label: "Face ID", icon: "faceid") {
                    authenticateWithBiometrics()
                }
                PinButton(label: "0") { pin.append("0") }
                PinButton(label: "←", icon: "delete.left") {
                    if !pin.isEmpty { pin.removeLast() }
                }
            }
            .padding(.horizontal, 40)

            Spacer()
        }
        .onChange(of: pin) { newValue in
            if newValue.count == 6 {
                authVM.login(pin: newValue)
                if !authVM.isAuthenticated { pin = "" }
            }
        }
    }

    private func authenticateWithBiometrics() {
        BiometricAuth.authenticate { success in
            if success {
                // Use stored last-user PIN from keychain
                if let storedPin = BiometricAuth.getStoredPin() {
                    authVM.login(pin: storedPin)
                }
            }
        }
    }
}

struct PinButton: View {
    let label: String
    var icon: String? = nil
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            if let icon = icon {
                Image(systemName: icon)
                    .font(.title2)
                    .frame(width: 72, height: 72)
            } else {
                Text(label)
                    .font(.title)
                    .frame(width: 72, height: 72)
            }
        }
        .foregroundColor(.primary)
        .background(Color(.systemGray6))
        .clipShape(Circle())
    }
}
