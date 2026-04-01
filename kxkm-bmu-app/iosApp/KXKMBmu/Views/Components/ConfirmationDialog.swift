import SwiftUI

struct ConfirmationDialog: ViewModifier {
    @Binding var isPresented: Bool
    let title: String
    let message: String
    let action: () -> Void

    func body(content: Content) -> some View {
        content
            .alert(title, isPresented: $isPresented) {
                Button("Annuler", role: .cancel) {}
                Button("Confirmer", role: .destructive) { action() }
            } message: {
                Text(message)
            }
    }
}

extension View {
    func confirmAction(isPresented: Binding<Bool>, title: String, message: String, action: @escaping () -> Void) -> some View {
        modifier(ConfirmationDialog(isPresented: isPresented, title: title, message: message, action: action))
    }
}
