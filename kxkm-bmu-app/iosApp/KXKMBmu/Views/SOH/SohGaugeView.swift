import SwiftUI

struct SohGaugeView: View {
    let sohPercent: Int
    var size: CGFloat = 72

    private var color: Color {
        switch sohPercent {
        case 80...100: return .green
        case 60..<80: return .orange
        default: return .red
        }
    }

    var body: some View {
        ZStack {
            Circle()
                .trim(from: 0.125, to: 0.875)
                .stroke(color.opacity(0.15), style: StrokeStyle(lineWidth: 6, lineCap: .round))
                .rotationEffect(.degrees(90))
                .frame(width: size, height: size)

            Circle()
                .trim(from: 0.125, to: 0.125 + 0.75 * Double(min(max(sohPercent, 0), 100)) / 100.0)
                .stroke(color, style: StrokeStyle(lineWidth: 6, lineCap: .round))
                .rotationEffect(.degrees(90))
                .frame(width: size, height: size)

            Text("\(sohPercent)%")
                .font(.system(size: size > 64 ? 16 : 12, weight: .bold, design: .monospaced))
                .foregroundColor(color)
        }
    }
}
