import SwiftUI

struct OnboardingView: View {
    @EnvironmentObject var authVM: AuthViewModel
    @State private var name = ""
    @State private var pin = ""
    @State private var confirmPin = ""
    @State private var step = 0

    var body: some View {
        VStack(spacing: 24) {
            Spacer()

            Image(systemName: "battery.100.bolt")
                .font(.system(size: 60))
                .foregroundColor(.green)

            Text("Configuration initiale")
                .font(.title2.bold())

            if step == 0 {
                TextField("Votre nom", text: $name)
                    .textFieldStyle(.roundedBorder)
                    .padding(.horizontal, 40)

                Button("Suivant") { step = 1 }
                    .disabled(name.isEmpty)
                    .buttonStyle(.borderedProminent)
            } else if step == 1 {
                Text("Choisissez un PIN (6 chiffres)")
                    .foregroundColor(.secondary)
                SecureField("PIN", text: $pin)
                    .textFieldStyle(.roundedBorder)
                    .keyboardType(.numberPad)
                    .padding(.horizontal, 40)

                Button("Suivant") { step = 2 }
                    .disabled(pin.count != 6)
                    .buttonStyle(.borderedProminent)
            } else {
                Text("Confirmez le PIN")
                    .foregroundColor(.secondary)
                SecureField("PIN", text: $confirmPin)
                    .textFieldStyle(.roundedBorder)
                    .keyboardType(.numberPad)
                    .padding(.horizontal, 40)

                if confirmPin.count == 6 && confirmPin != pin {
                    Text("Les PINs ne correspondent pas")
                        .foregroundColor(.red)
                        .font(.caption)
                }

                Button("Créer le compte Admin") {
                    authVM.createAdmin(name: name, pin: pin)
                    authVM.login(pin: pin)
                }
                .disabled(confirmPin != pin || pin.count != 6)
                .buttonStyle(.borderedProminent)
            }

            Spacer()
        }
    }
}
