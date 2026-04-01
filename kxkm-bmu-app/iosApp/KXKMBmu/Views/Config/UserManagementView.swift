import SwiftUI
import Shared

struct UserManagementView: View {
    @EnvironmentObject var vm: ConfigViewModel
    @State private var showAddUser = false
    @State private var newName = ""
    @State private var newPin = ""
    @State private var newRole: UserRole = .technician

    var body: some View {
        List {
            ForEach(vm.users, id: \.id) { user in
                HStack {
                    VStack(alignment: .leading) {
                        Text(user.name).font(.body.bold())
                        Text(user.role.displayName).font(.caption).foregroundColor(.secondary)
                    }
                    Spacer()
                    if user.role != .admin {
                        Button(role: .destructive) { vm.deleteUser(user) } label: {
                            Image(systemName: "trash")
                        }
                    }
                }
            }

            Button { showAddUser = true } label: {
                Label("Ajouter un utilisateur", systemImage: "plus")
            }
        }
        .navigationTitle("Utilisateurs")
        .sheet(isPresented: $showAddUser) {
            NavigationStack {
                Form {
                    TextField("Nom", text: $newName)
                    SecureField("PIN (6 chiffres)", text: $newPin)
                        .keyboardType(.numberPad)
                    Picker("Rôle", selection: $newRole) {
                        Text("Technicien").tag(UserRole.technician)
                        Text("Lecteur").tag(UserRole.viewer)
                    }
                }
                .navigationTitle("Nouveau profil")
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) {
                        Button("Annuler") { showAddUser = false }
                    }
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Créer") {
                            vm.createUser(name: newName, pin: newPin, role: newRole)
                            showAddUser = false
                            newName = ""; newPin = ""
                        }
                        .disabled(newName.isEmpty || newPin.count != 6)
                    }
                }
            }
        }
    }
}
