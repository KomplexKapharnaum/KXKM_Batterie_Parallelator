import SwiftUI
import Shared

struct BatteryStateIcon: View {
    let state: BatteryStatus

    var body: some View {
        Image(systemName: state.icon)
            .foregroundColor(state.color)
            .font(.caption)
    }
}
