import SwiftUI
// import Shared — using Stubs

struct AuditView: View {
    @EnvironmentObject var vm: AuditViewModel

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                // Sync indicator
                if vm.pendingSyncCount > 0 {
                    HStack {
                        Image(systemName: "icloud.and.arrow.up")
                        Text("\(vm.pendingSyncCount) événements en attente de sync")
                            .font(.caption)
                    }
                    .foregroundColor(.orange)
                    .padding(.vertical, 4)
                }

                // Filter bar
                ScrollView(.horizontal, showsIndicators: false) {
                    HStack {
                        FilterChip("Tous", isActive: vm.filterAction == nil) {
                            vm.clearFilters()
                        }
                        FilterChip("Switch", isActive: vm.filterAction == "switch_on" || vm.filterAction == "switch_off") {
                            vm.filterAction = "switch"
                            vm.reload()
                        }
                        FilterChip("Config", isActive: vm.filterAction == "config_change") {
                            vm.filterAction = "config_change"
                            vm.reload()
                        }
                        FilterChip("Reset", isActive: vm.filterAction == "reset") {
                            vm.filterAction = "reset"
                            vm.reload()
                        }
                    }
                    .padding(.horizontal)
                }
                .padding(.vertical, 8)

                // Event list
                List {
                    ForEach(vm.events, id: \.timestamp) { event in
                        AuditRowView(event: event)
                    }
                }
                .listStyle(.plain)
            }
            .navigationTitle("Audit")
            .onAppear { vm.reload() }
        }
    }
}

struct FilterChip: View {
    let label: String
    let isActive: Bool
    let action: () -> Void

    init(_ label: String, isActive: Bool, action: @escaping () -> Void) {
        self.label = label
        self.isActive = isActive
        self.action = action
    }

    var body: some View {
        Button(action: action) {
            Text(label)
                .font(.caption.bold())
                .padding(.horizontal, 12)
                .padding(.vertical, 6)
                .background(isActive ? Color.accentColor : Color(.systemGray5))
                .foregroundColor(isActive ? .white : .primary)
                .cornerRadius(16)
        }
    }
}
