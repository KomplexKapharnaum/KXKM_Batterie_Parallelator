import SwiftUI

struct DeviceSelectorView: View {
    @ObservedObject var ble = BleManager.shared

    var body: some View {
        VStack(spacing: 16) {
            // Header
            HStack {
                Image(systemName: "antenna.radiowaves.left.and.right")
                    .font(.title2)
                    .foregroundColor(.green)
                Text("BMU à proximité")
                    .font(.title3.bold())
                Spacer()
                if ble.isScanning {
                    ProgressView()
                        .scaleEffect(0.8)
                }
            }
            .padding(.horizontal)

            if ble.discoveredDevices.isEmpty && !ble.isScanning {
                VStack(spacing: 12) {
                    Image(systemName: "bolt.slash")
                        .font(.system(size: 40))
                        .foregroundColor(.secondary)
                    Text("Aucun BMU détecté")
                        .foregroundColor(.secondary)
                    Button("Relancer le scan") {
                        ble.startScan()
                    }
                    .buttonStyle(.borderedProminent)
                    .tint(.green)
                }
                .padding(.top, 40)
            } else if ble.discoveredDevices.isEmpty && ble.isScanning {
                VStack(spacing: 12) {
                    ProgressView()
                        .scaleEffect(1.5)
                    Text("Recherche de BMU...")
                        .foregroundColor(.secondary)
                        .padding(.top, 8)
                }
                .padding(.top, 40)
            } else {
                LazyVStack(spacing: 8) {
                    ForEach(ble.discoveredDevices) { device in
                        Button {
                            ble.connectTo(device)
                        } label: {
                            HStack(spacing: 12) {
                                Image(systemName: "battery.100.bolt")
                                    .font(.title2)
                                    .foregroundColor(.green)
                                    .frame(width: 40)

                                VStack(alignment: .leading, spacing: 2) {
                                    Text(device.deviceName)
                                        .font(.headline)
                                        .foregroundColor(.primary)
                                    Text(device.name)
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }

                                Spacer()

                                // Signal strength
                                signalBars(device.rssi)

                                Text("\(device.rssi) dBm")
                                    .font(.caption2)
                                    .foregroundColor(.secondary)

                                Image(systemName: "chevron.right")
                                    .foregroundColor(.secondary)
                            }
                            .padding(12)
                            .background(Color(.systemGray6))
                            .cornerRadius(12)
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.horizontal)
            }

            Spacer()

            HStack(spacing: 12) {
                if !ble.isScanning {
                    Button {
                        ble.startScan()
                    } label: {
                        Label("Actualiser", systemImage: "arrow.clockwise")
                    }
                    .buttonStyle(.bordered)
                }

                #if DEBUG
                Button {
                    // Force connected state for demo mode
                    ble.isConnected = true
                    ble.connectedDeviceName = "demo"
                } label: {
                    Label("Mode démo", systemImage: "play.fill")
                }
                .buttonStyle(.bordered)
                .tint(.orange)
                #endif
            }
            .padding(.bottom)
        }
        .onAppear {
            if !ble.isConnected {
                ble.startScan()
            }
        }
    }

    private func signalBars(_ rssi: Int) -> some View {
        HStack(spacing: 2) {
            ForEach(0..<4, id: \.self) { i in
                RoundedRectangle(cornerRadius: 1)
                    .fill(barColor(rssi, bar: i))
                    .frame(width: 4, height: CGFloat(6 + i * 4))
            }
        }
    }

    private func barColor(_ rssi: Int, bar: Int) -> Color {
        let threshold: [Int] = [-90, -75, -60, -45]
        return rssi > threshold[bar] ? .green : Color(.systemGray4)
    }
}
