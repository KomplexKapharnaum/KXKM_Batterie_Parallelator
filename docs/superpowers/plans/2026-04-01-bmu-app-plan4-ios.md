# BMU App — Plan 4: iOS SwiftUI App

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the iOS native UI for the KXKM BMU companion app using SwiftUI, consuming the KMP shared module for all business logic, transport, auth, and data persistence.

**Architecture:** Thin SwiftUI layer over the KMP shared module. ViewModels wrap shared use cases and expose `@Published` state. Navigation via TabView (5 tabs). The shared module handles BLE, WiFi, MQTT, REST, SQLDelight, auth — iOS only does UI + platform permissions.

**Tech Stack:** SwiftUI, KMP shared framework (via SPM or CocoaPods), Combine, Swift Charts

**Depends on:** Plan 2 (KMP shared module must be built first)

**Spec:** `docs/superpowers/specs/2026-04-01-smartphone-app-design.md`

---

## File Structure

```
iosApp/
├── KXKMBmu.xcodeproj
├── KXKMBmu/
│   ├── App/
│   │   ├── KXKMBmuApp.swift              # App entry, DI setup
│   │   └── ContentView.swift             # TabView root (5 tabs)
│   ├── ViewModels/
│   │   ├── DashboardViewModel.swift       # Battery grid state
│   │   ├── BatteryDetailViewModel.swift   # Single battery + history chart
│   │   ├── SystemViewModel.swift          # System info + solar
│   │   ├── AuditViewModel.swift           # Audit trail + filters
│   │   ├── ConfigViewModel.swift          # Config + WiFi + users
│   │   └── AuthViewModel.swift            # PIN entry, session, role
│   ├── Views/
│   │   ├── Auth/
│   │   │   ├── PinEntryView.swift         # PIN pad (6 digits)
│   │   │   └── OnboardingView.swift       # First-launch admin setup
│   │   ├── Dashboard/
│   │   │   ├── DashboardView.swift        # Battery grid
│   │   │   └── BatteryCellView.swift      # Single battery card
│   │   ├── Detail/
│   │   │   ├── BatteryDetailView.swift    # Full detail screen
│   │   │   └── VoltageChartView.swift     # Swift Charts history
│   │   ├── System/
│   │   │   └── SystemView.swift           # System + solar info
│   │   ├── Audit/
│   │   │   ├── AuditView.swift            # Event list + filters
│   │   │   └── AuditRowView.swift         # Single event row
│   │   ├── Config/
│   │   │   ├── ConfigView.swift           # Settings root
│   │   │   ├── ProtectionConfigView.swift # V/I thresholds
│   │   │   ├── WifiConfigView.swift       # BMU WiFi setup
│   │   │   ├── UserManagementView.swift   # User CRUD
│   │   │   ├── SyncConfigView.swift       # Cloud sync settings
│   │   │   └── TransportConfigView.swift  # Channel selection
│   │   └── Components/
│   │       ├── StatusBarView.swift        # Transport indicator
│   │       ├── BatteryStateIcon.swift     # Color-coded state icon
│   │       └── ConfirmationDialog.swift   # Switch ON/OFF confirm
│   ├── Helpers/
│   │   ├── SharedBridge.swift             # KMP↔Swift type mapping
│   │   └── BiometricAuth.swift            # Face ID / Touch ID
│   └── Resources/
│       ├── Assets.xcassets
│       └── Info.plist                     # BLE + camera permissions
```

---

### Task 1: Xcode project setup + KMP framework integration

**Files:**
- Create: `iosApp/KXKMBmu.xcodeproj`
- Create: `iosApp/KXKMBmu/App/KXKMBmuApp.swift`

- [ ] **Step 1: Create Xcode project**

In the `kxkm-bmu-app/` root (created by Plan 2), create the iOS project:

```bash
cd kxkm-bmu-app
mkdir -p iosApp/KXKMBmu/App
```

Create `iosApp/KXKMBmu/App/KXKMBmuApp.swift`:

```swift
import SwiftUI
import Shared // KMP shared framework

@main
struct KXKMBmuApp: App {
    @StateObject private var authVM = AuthViewModel()

    var body: some Scene {
        WindowGroup {
            if authVM.isAuthenticated {
                ContentView()
                    .environmentObject(authVM)
            } else if authVM.needsOnboarding {
                OnboardingView()
                    .environmentObject(authVM)
            } else {
                PinEntryView()
                    .environmentObject(authVM)
            }
        }
    }
}
```

- [ ] **Step 2: Configure KMP framework in build**

In the KMP `shared/build.gradle.kts`, the iOS framework should already be configured (Plan 2). Verify the framework is exported:

```kotlin
// In shared/build.gradle.kts — should exist from Plan 2
kotlin {
    iosX64()
    iosArm64()
    iosSimulatorArm64()

    cocoapods {
        summary = "KXKM BMU shared logic"
        homepage = "https://github.com/kxkm/bmu-app"
        ios.deploymentTarget = "16.0"
        framework {
            baseName = "Shared"
        }
    }
}
```

Run `./gradlew :shared:podspec` to generate the CocoaPods podspec, then in the iosApp directory:

```bash
cd iosApp
pod init
# Edit Podfile to add: pod 'Shared', :path => '../shared'
pod install
```

- [ ] **Step 3: Add Info.plist permissions**

Create `iosApp/KXKMBmu/Resources/Info.plist` with BLE permissions:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>NSBluetoothAlwaysUsageDescription</key>
    <string>KXKM BMU uses Bluetooth to communicate with the battery management unit.</string>
    <key>NSBluetoothPeripheralUsageDescription</key>
    <string>KXKM BMU uses Bluetooth to communicate with the battery management unit.</string>
    <key>NSLocalNetworkUsageDescription</key>
    <string>KXKM BMU connects to the BMU on your local network.</string>
    <key>NSFaceIDUsageDescription</key>
    <string>Use Face ID to unlock KXKM BMU app.</string>
</dict>
</plist>
```

- [ ] **Step 4: Build to verify framework links**

```bash
cd iosApp && xcodebuild -workspace KXKMBmu.xcworkspace -scheme KXKMBmu -sdk iphonesimulator build 2>&1 | tail -5
```
Expected: `BUILD SUCCEEDED`

- [ ] **Step 5: Commit**

```bash
git add iosApp/
git commit -m "feat(ios): Xcode project setup with KMP shared framework"
```

---

### Task 2: KMP↔Swift bridge helpers

**Files:**
- Create: `iosApp/KXKMBmu/Helpers/SharedBridge.swift`

- [ ] **Step 1: Create type bridge**

KMP exports Kotlin types to Swift via Objective-C interop. Some types need convenience extensions:

```swift
import Shared

// MARK: - BatteryStatus convenience

extension BatteryStatus {
    var displayName: String {
        switch self {
        case .connected: return "Connecté"
        case .disconnected: return "Déconnecté"
        case .reconnecting: return "Reconnexion"
        case .error: return "Erreur"
        case .locked: return "Verrouillé"
        default: return "Inconnu"
        }
    }

    var color: Color {
        switch self {
        case .connected: return .green
        case .disconnected: return .red
        case .reconnecting: return .yellow
        case .error: return .orange
        case .locked: return .red
        default: return .gray
        }
    }

    var icon: String {
        switch self {
        case .connected: return "bolt.fill"
        case .disconnected: return "bolt.slash"
        case .reconnecting: return "arrow.clockwise"
        case .error: return "exclamationmark.triangle"
        case .locked: return "lock.fill"
        default: return "questionmark"
        }
    }
}

// MARK: - UserRole convenience

extension UserRole {
    var displayName: String {
        switch self {
        case .admin: return "Admin"
        case .technician: return "Technicien"
        case .viewer: return "Lecteur"
        default: return "Inconnu"
        }
    }

    var canControl: Bool {
        self == .admin || self == .technician
    }

    var canConfigure: Bool {
        self == .admin
    }
}

// MARK: - Transport channel display

extension TransportChannel {
    var displayName: String {
        switch self {
        case .ble: return "BLE"
        case .wifi: return "WiFi"
        case .mqttCloud: return "Cloud MQTT"
        case .restCloud: return "Cloud REST"
        case .offline: return "Hors ligne"
        default: return "—"
        }
    }

    var icon: String {
        switch self {
        case .ble: return "antenna.radiowaves.left.and.right"
        case .wifi: return "wifi"
        case .mqttCloud, .restCloud: return "cloud"
        case .offline: return "icloud.slash"
        default: return "questionmark"
        }
    }
}

// MARK: - Voltage formatting

extension Int32 {
    /// Convert mV to display string "XX.XX V"
    var voltageDisplay: String {
        String(format: "%.2f V", Double(self) / 1000.0)
    }

    /// Convert mA to display string "X.XX A"
    var currentDisplay: String {
        String(format: "%.2f A", Double(self) / 1000.0)
    }

    /// Convert mAh to display string "X.XX Ah"
    var ahDisplay: String {
        String(format: "%.2f Ah", Double(self) / 1000.0)
    }
}
```

- [ ] **Step 2: Build to verify**

```bash
xcodebuild -workspace KXKMBmu.xcworkspace -scheme KXKMBmu -sdk iphonesimulator build 2>&1 | tail -3
```

- [ ] **Step 3: Commit**

```bash
git add iosApp/KXKMBmu/Helpers/SharedBridge.swift
git commit -m "feat(ios): KMP-Swift bridge helpers (display names, colors, formatting)"
```

---

### Task 3: Auth — PIN entry + onboarding

**Files:**
- Create: `iosApp/KXKMBmu/ViewModels/AuthViewModel.swift`
- Create: `iosApp/KXKMBmu/Views/Auth/PinEntryView.swift`
- Create: `iosApp/KXKMBmu/Views/Auth/OnboardingView.swift`
- Create: `iosApp/KXKMBmu/Helpers/BiometricAuth.swift`

- [ ] **Step 1: Create AuthViewModel**

```swift
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
```

- [ ] **Step 2: Create PinEntryView**

```swift
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
```

- [ ] **Step 3: Create OnboardingView**

```swift
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
```

- [ ] **Step 4: Create BiometricAuth helper**

```swift
import LocalAuthentication

enum BiometricAuth {
    static func authenticate(completion: @escaping (Bool) -> Void) {
        let context = LAContext()
        var error: NSError?

        guard context.canEvaluatePolicy(.deviceOwnerAuthenticationWithBiometrics, error: &error) else {
            completion(false)
            return
        }

        context.evaluatePolicy(.deviceOwnerAuthenticationWithBiometrics,
                               localizedReason: "Déverrouiller KXKM BMU") { success, _ in
            DispatchQueue.main.async { completion(success) }
        }
    }

    static func getStoredPin() -> String? {
        // Read from Keychain
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: "com.kxkm.bmu.pin",
            kSecReturnData as String: true
        ]
        var item: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &item) == errSecSuccess,
              let data = item as? Data,
              let pin = String(data: data, encoding: .utf8) else { return nil }
        return pin
    }

    static func storePin(_ pin: String) {
        let data = pin.data(using: .utf8)!
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: "com.kxkm.bmu.pin",
            kSecValueData as String: data
        ]
        SecItemDelete(query as CFDictionary) // Remove old
        SecItemAdd(query as CFDictionary, nil)
    }
}
```

- [ ] **Step 5: Build to verify**

```bash
xcodebuild -workspace KXKMBmu.xcworkspace -scheme KXKMBmu -sdk iphonesimulator build 2>&1 | tail -3
```

- [ ] **Step 6: Commit**

```bash
git add iosApp/KXKMBmu/ViewModels/AuthViewModel.swift \
        iosApp/KXKMBmu/Views/Auth/ \
        iosApp/KXKMBmu/Helpers/BiometricAuth.swift
git commit -m "feat(ios): PIN entry, onboarding, biometric auth"
```

---

### Task 4: ContentView — TabView navigation

**Files:**
- Create: `iosApp/KXKMBmu/App/ContentView.swift`
- Create: `iosApp/KXKMBmu/Views/Components/StatusBarView.swift`

- [ ] **Step 1: Create ContentView with 5 tabs**

```swift
import SwiftUI

struct ContentView: View {
    @EnvironmentObject var authVM: AuthViewModel
    @StateObject private var dashboardVM = DashboardViewModel()
    @StateObject private var systemVM = SystemViewModel()
    @StateObject private var auditVM = AuditViewModel()
    @StateObject private var configVM = ConfigViewModel()

    var body: some View {
        VStack(spacing: 0) {
            StatusBarView()

            TabView {
                DashboardView()
                    .environmentObject(dashboardVM)
                    .tabItem {
                        Label("Batteries", systemImage: "bolt.fill")
                    }

                SystemView()
                    .environmentObject(systemVM)
                    .tabItem {
                        Label("Système", systemImage: "gearshape")
                    }

                AuditView()
                    .environmentObject(auditVM)
                    .tabItem {
                        Label("Audit", systemImage: "list.clipboard")
                    }

                if authVM.currentUser?.role.canConfigure == true {
                    ConfigView()
                        .environmentObject(configVM)
                        .tabItem {
                            Label("Config", systemImage: "wrench")
                        }
                }
            }
        }
    }
}
```

- [ ] **Step 2: Create StatusBarView**

```swift
import SwiftUI
import Shared

struct StatusBarView: View {
    @StateObject private var transport = TransportStatusViewModel()

    var body: some View {
        HStack {
            Image(systemName: transport.channel.icon)
                .foregroundColor(transport.isConnected ? .green : .orange)
            Text(transport.channel.displayName)
                .font(.caption.bold())

            Spacer()

            if let deviceName = transport.deviceName {
                Text(deviceName)
                    .font(.caption)
                    .foregroundColor(.secondary)
            }

            if let rssi = transport.rssi {
                Image(systemName: rssiIcon(rssi))
                    .foregroundColor(.secondary)
                Text("\(rssi) dBm")
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
        .padding(.horizontal)
        .padding(.vertical, 6)
        .background(Color(.systemGray6))
    }

    private func rssiIcon(_ rssi: Int) -> String {
        if rssi > -50 { return "wifi" }
        if rssi > -70 { return "wifi" }
        return "wifi.exclamationmark"
    }
}

class TransportStatusViewModel: ObservableObject {
    @Published var channel: TransportChannel = .offline
    @Published var isConnected = false
    @Published var deviceName: String? = nil
    @Published var rssi: Int? = nil

    init() {
        // Subscribe to shared TransportManager state
        let manager = SharedFactory.companion.createTransportManager()
        // KMP Flow → Combine bridge (via SKIE or manual collector)
        // This will be wired when Plan 2 defines the exact Flow API
    }
}
```

- [ ] **Step 3: Build to verify**

```bash
xcodebuild -workspace KXKMBmu.xcworkspace -scheme KXKMBmu -sdk iphonesimulator build 2>&1 | tail -3
```

- [ ] **Step 4: Commit**

```bash
git add iosApp/KXKMBmu/App/ContentView.swift \
        iosApp/KXKMBmu/Views/Components/StatusBarView.swift
git commit -m "feat(ios): TabView navigation + transport status bar"
```

---

### Task 5: Dashboard — battery grid

**Files:**
- Create: `iosApp/KXKMBmu/ViewModels/DashboardViewModel.swift`
- Create: `iosApp/KXKMBmu/Views/Dashboard/DashboardView.swift`
- Create: `iosApp/KXKMBmu/Views/Dashboard/BatteryCellView.swift`
- Create: `iosApp/KXKMBmu/Views/Components/BatteryStateIcon.swift`

- [ ] **Step 1: Create DashboardViewModel**

```swift
import SwiftUI
import Shared
import Combine

class DashboardViewModel: ObservableObject {
    @Published var batteries: [BatteryState] = []
    @Published var isLoading = true

    private let monitorUseCase: MonitoringUseCase

    init() {
        self.monitorUseCase = SharedFactory.companion.createMonitoringUseCase()
        startObserving()
    }

    private func startObserving() {
        // Collect KMP StateFlow<List<BatteryState>> into @Published
        // Exact bridge depends on Plan 2 (SKIE or manual FlowCollector)
        monitorUseCase.observeBatteries { [weak self] states in
            DispatchQueue.main.async {
                self?.batteries = states
                self?.isLoading = false
            }
        }
    }
}
```

- [ ] **Step 2: Create BatteryStateIcon**

```swift
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
```

- [ ] **Step 3: Create BatteryCellView**

```swift
import SwiftUI
import Shared

struct BatteryCellView: View {
    let battery: BatteryState

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                Text("Bat \(battery.index + 1)")
                    .font(.caption.bold())
                Spacer()
                BatteryStateIcon(state: battery.state)
            }

            Text(battery.voltageMv.voltageDisplay)
                .font(.title3.monospacedDigit().bold())
                .foregroundColor(voltageColor)

            Text(battery.currentMa.currentDisplay)
                .font(.caption.monospacedDigit())
                .foregroundColor(.secondary)

            HStack {
                Image(systemName: "arrow.up.arrow.down")
                    .font(.caption2)
                Text("\(battery.nbSwitch)")
                    .font(.caption2)
            }
            .foregroundColor(.secondary)
        }
        .padding(10)
        .background(Color(.systemGray6))
        .cornerRadius(12)
        .overlay(
            RoundedRectangle(cornerRadius: 12)
                .stroke(battery.state.color.opacity(0.5), lineWidth: 2)
        )
    }

    private var voltageColor: Color {
        let mv = battery.voltageMv
        if mv < 24000 || mv > 30000 { return .red }
        if mv < 24500 || mv > 29500 { return .orange }
        return .primary
    }
}
```

- [ ] **Step 4: Create DashboardView**

```swift
import SwiftUI
import Shared

struct DashboardView: View {
    @EnvironmentObject var vm: DashboardViewModel

    private let columns = [
        GridItem(.adaptive(minimum: 140, maximum: 200))
    ]

    var body: some View {
        NavigationStack {
            ScrollView {
                if vm.isLoading {
                    ProgressView("Connexion au BMU...")
                        .padding(.top, 60)
                } else if vm.batteries.isEmpty {
                    VStack(spacing: 12) {
                        Image(systemName: "bolt.slash")
                            .font(.system(size: 40))
                            .foregroundColor(.secondary)
                        Text("Aucune batterie détectée")
                            .foregroundColor(.secondary)
                    }
                    .padding(.top, 60)
                } else {
                    LazyVGrid(columns: columns, spacing: 12) {
                        ForEach(vm.batteries, id: \.index) { battery in
                            NavigationLink(value: battery.index) {
                                BatteryCellView(battery: battery)
                            }
                            .buttonStyle(.plain)
                        }
                    }
                    .padding()
                }
            }
            .navigationTitle("Batteries")
            .navigationDestination(for: Int32.self) { index in
                BatteryDetailView(batteryIndex: Int(index))
            }
        }
    }
}
```

- [ ] **Step 5: Build to verify**

```bash
xcodebuild -workspace KXKMBmu.xcworkspace -scheme KXKMBmu -sdk iphonesimulator build 2>&1 | tail -3
```

- [ ] **Step 6: Commit**

```bash
git add iosApp/KXKMBmu/ViewModels/DashboardViewModel.swift \
        iosApp/KXKMBmu/Views/Dashboard/ \
        iosApp/KXKMBmu/Views/Components/BatteryStateIcon.swift
git commit -m "feat(ios): dashboard battery grid with adaptive layout"
```

---

### Task 6: Battery detail — info + chart + controls

**Files:**
- Create: `iosApp/KXKMBmu/ViewModels/BatteryDetailViewModel.swift`
- Create: `iosApp/KXKMBmu/Views/Detail/BatteryDetailView.swift`
- Create: `iosApp/KXKMBmu/Views/Detail/VoltageChartView.swift`
- Create: `iosApp/KXKMBmu/Views/Components/ConfirmationDialog.swift`

- [ ] **Step 1: Create BatteryDetailViewModel**

```swift
import SwiftUI
import Shared

class BatteryDetailViewModel: ObservableObject {
    @Published var battery: BatteryState?
    @Published var history: [BatteryHistoryPoint] = []
    @Published var events: [AuditEvent] = []
    @Published var commandResult: String? = nil

    let batteryIndex: Int
    private let monitorUseCase: MonitoringUseCase
    private let controlUseCase: ControlUseCase

    init(batteryIndex: Int) {
        self.batteryIndex = batteryIndex
        self.monitorUseCase = SharedFactory.companion.createMonitoringUseCase()
        self.controlUseCase = SharedFactory.companion.createControlUseCase()
        startObserving()
        loadHistory()
    }

    private func startObserving() {
        monitorUseCase.observeBattery(index: Int32(batteryIndex)) { [weak self] state in
            DispatchQueue.main.async { self?.battery = state }
        }
    }

    private func loadHistory() {
        monitorUseCase.getHistory(batteryIndex: Int32(batteryIndex), hours: 24) { [weak self] points in
            DispatchQueue.main.async { self?.history = points }
        }
    }

    func switchBattery(on: Bool) {
        controlUseCase.switchBattery(index: Int32(batteryIndex), on: on) { [weak self] result in
            DispatchQueue.main.async {
                self?.commandResult = result.isSuccess ? "OK" : "Erreur: \(result.errorMessage ?? "")"
            }
        }
    }

    func resetSwitchCount() {
        controlUseCase.resetSwitchCount(index: Int32(batteryIndex)) { [weak self] result in
            DispatchQueue.main.async {
                self?.commandResult = result.isSuccess ? "Compteur remis à zéro" : "Erreur"
            }
        }
    }
}
```

- [ ] **Step 2: Create VoltageChartView**

```swift
import SwiftUI
import Charts
import Shared

struct VoltageChartView: View {
    let history: [BatteryHistoryPoint]

    var body: some View {
        if history.isEmpty {
            Text("Pas d'historique disponible")
                .foregroundColor(.secondary)
                .frame(height: 200)
        } else {
            Chart {
                ForEach(history, id: \.timestamp) { point in
                    LineMark(
                        x: .value("Temps", Date(timeIntervalSince1970: Double(point.timestamp) / 1000.0)),
                        y: .value("Tension (V)", Double(point.voltageMv) / 1000.0)
                    )
                    .foregroundStyle(.blue)
                }
            }
            .chartYScale(domain: 22...32)
            .chartYAxisLabel("Tension (V)")
            .chartXAxis {
                AxisMarks(values: .stride(by: .hour, count: 2)) {
                    AxisValueLabel(format: .dateTime.hour())
                }
            }
            .frame(height: 200)
        }
    }
}
```

- [ ] **Step 3: Create ConfirmationDialog**

```swift
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
```

- [ ] **Step 4: Create BatteryDetailView**

```swift
import SwiftUI
import Shared

struct BatteryDetailView: View {
    @StateObject private var vm: BatteryDetailViewModel
    @EnvironmentObject var authVM: AuthViewModel
    @State private var showSwitchConfirm = false
    @State private var switchAction = true // true = ON

    init(batteryIndex: Int) {
        _vm = StateObject(wrappedValue: BatteryDetailViewModel(batteryIndex: batteryIndex))
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 20) {
                // Current state card
                if let bat = vm.battery {
                    stateCard(bat)
                }

                // Chart
                GroupBox("Historique tension (24h)") {
                    VoltageChartView(history: vm.history)
                }

                // Counters
                if let bat = vm.battery {
                    countersSection(bat)
                }

                // Controls (role-gated)
                if authVM.currentUser?.role.canControl == true {
                    controlsSection
                }

                // Command result
                if let result = vm.commandResult {
                    Text(result)
                        .font(.caption)
                        .foregroundColor(result == "OK" ? .green : .red)
                        .padding(.horizontal)
                }
            }
            .padding()
        }
        .navigationTitle("Batterie \(vm.batteryIndex + 1)")
        .confirmAction(
            isPresented: $showSwitchConfirm,
            title: switchAction ? "Connecter batterie ?" : "Déconnecter batterie ?",
            message: "Batterie \(vm.batteryIndex + 1) — cette action est enregistrée dans l'audit."
        ) {
            vm.switchBattery(on: switchAction)
        }
    }

    private func stateCard(_ bat: BatteryState) -> some View {
        HStack {
            VStack(alignment: .leading, spacing: 4) {
                Text(bat.voltageMv.voltageDisplay)
                    .font(.system(.largeTitle, design: .monospaced).bold())
                Text(bat.currentMa.currentDisplay)
                    .font(.title3.monospacedDigit())
                    .foregroundColor(.secondary)
            }
            Spacer()
            VStack {
                BatteryStateIcon(state: bat.state)
                    .font(.title)
                Text(bat.state.displayName)
                    .font(.caption)
            }
        }
        .padding()
        .background(Color(.systemGray6))
        .cornerRadius(12)
    }

    private func countersSection(_ bat: BatteryState) -> some View {
        GroupBox("Compteurs") {
            VStack(spacing: 8) {
                HStack {
                    Text("Décharge")
                    Spacer()
                    Text(bat.ahDischargeMah.ahDisplay)
                        .monospacedDigit()
                }
                HStack {
                    Text("Charge")
                    Spacer()
                    Text(bat.ahChargeMah.ahDisplay)
                        .monospacedDigit()
                }
                HStack {
                    Text("Nb switches")
                    Spacer()
                    Text("\(bat.nbSwitch)")
                        .monospacedDigit()
                }
            }
        }
    }

    private var controlsSection: some View {
        GroupBox("Contrôle") {
            HStack(spacing: 12) {
                Button {
                    switchAction = true
                    showSwitchConfirm = true
                } label: {
                    Label("Connecter", systemImage: "bolt.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(.green)

                Button {
                    switchAction = false
                    showSwitchConfirm = true
                } label: {
                    Label("Déconnecter", systemImage: "bolt.slash")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .tint(.red)
            }

            Button {
                vm.resetSwitchCount()
            } label: {
                Label("Reset compteur", systemImage: "arrow.counterclockwise")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
        }
    }
}
```

- [ ] **Step 5: Build to verify**

```bash
xcodebuild -workspace KXKMBmu.xcworkspace -scheme KXKMBmu -sdk iphonesimulator build 2>&1 | tail -3
```

- [ ] **Step 6: Commit**

```bash
git add iosApp/KXKMBmu/ViewModels/BatteryDetailViewModel.swift \
        iosApp/KXKMBmu/Views/Detail/ \
        iosApp/KXKMBmu/Views/Components/ConfirmationDialog.swift
git commit -m "feat(ios): battery detail with chart, counters, and controls"
```

---

### Task 7: System screen — firmware + solar + connectivity

**Files:**
- Create: `iosApp/KXKMBmu/ViewModels/SystemViewModel.swift`
- Create: `iosApp/KXKMBmu/Views/System/SystemView.swift`

- [ ] **Step 1: Create SystemViewModel**

```swift
import SwiftUI
import Shared

class SystemViewModel: ObservableObject {
    @Published var system: SystemInfo? = nil
    @Published var solar: SolarData? = nil

    private let monitorUseCase: MonitoringUseCase

    init() {
        self.monitorUseCase = SharedFactory.companion.createMonitoringUseCase()
        startObserving()
    }

    private func startObserving() {
        monitorUseCase.observeSystem { [weak self] info in
            DispatchQueue.main.async { self?.system = info }
        }
        monitorUseCase.observeSolar { [weak self] data in
            DispatchQueue.main.async { self?.solar = data }
        }
    }
}
```

- [ ] **Step 2: Create SystemView**

```swift
import SwiftUI
import Shared

struct SystemView: View {
    @EnvironmentObject var vm: SystemViewModel

    var body: some View {
        NavigationStack {
            List {
                if let sys = vm.system {
                    Section("Firmware") {
                        row("Version", sys.firmwareVersion)
                        row("Uptime", formatUptime(sys.uptimeSeconds))
                        row("Heap libre", formatBytes(sys.heapFree))
                    }

                    Section("Topologie") {
                        row("INA237", "\(sys.nbIna)")
                        row("TCA9535", "\(sys.nbTca)")
                        HStack {
                            Text("Validation")
                            Spacer()
                            Image(systemName: sys.topologyValid ? "checkmark.circle.fill" : "xmark.circle.fill")
                                .foregroundColor(sys.topologyValid ? .green : .red)
                        }
                    }

                    Section("WiFi") {
                        row("IP", sys.wifiIp ?? "Non connecté")
                    }
                } else {
                    Section {
                        ProgressView("Chargement...")
                    }
                }

                if let solar = vm.solar {
                    Section("Solaire (VE.Direct)") {
                        row("Tension panneau", "\(Double(solar.panelVoltageMv) / 1000.0, specifier: "%.1f") V")
                        row("Puissance", "\(solar.panelPowerW) W")
                        row("Tension batterie", "\(Double(solar.batteryVoltageMv) / 1000.0, specifier: "%.1f") V")
                        row("Courant", "\(Double(solar.batteryCurrentMa) / 1000.0, specifier: "%.2f") A")
                        row("État charge", chargeStateName(solar.chargeState))
                        row("Production jour", "\(solar.yieldTodayWh) Wh")
                    }
                }
            }
            .navigationTitle("Système")
        }
    }

    private func row(_ label: String, _ value: String) -> some View {
        HStack {
            Text(label)
            Spacer()
            Text(value).foregroundColor(.secondary).monospacedDigit()
        }
    }

    private func formatUptime(_ seconds: Int64) -> String {
        let h = seconds / 3600
        let m = (seconds % 3600) / 60
        return "\(h)h \(m)m"
    }

    private func formatBytes(_ bytes: Int64) -> String {
        if bytes > 1_000_000 { return String(format: "%.1f MB", Double(bytes) / 1_000_000.0) }
        return String(format: "%.0f KB", Double(bytes) / 1000.0)
    }

    private func chargeStateName(_ cs: Int32) -> String {
        switch cs {
        case 0: return "Off"
        case 2: return "Fault"
        case 3: return "Bulk"
        case 4: return "Absorption"
        case 5: return "Float"
        default: return "État \(cs)"
        }
    }
}
```

- [ ] **Step 3: Build to verify**

```bash
xcodebuild -workspace KXKMBmu.xcworkspace -scheme KXKMBmu -sdk iphonesimulator build 2>&1 | tail -3
```

- [ ] **Step 4: Commit**

```bash
git add iosApp/KXKMBmu/ViewModels/SystemViewModel.swift \
        iosApp/KXKMBmu/Views/System/
git commit -m "feat(ios): system screen with firmware, topology, solar info"
```

---

### Task 8: Audit trail screen

**Files:**
- Create: `iosApp/KXKMBmu/ViewModels/AuditViewModel.swift`
- Create: `iosApp/KXKMBmu/Views/Audit/AuditView.swift`
- Create: `iosApp/KXKMBmu/Views/Audit/AuditRowView.swift`

- [ ] **Step 1: Create AuditViewModel**

```swift
import SwiftUI
import Shared

class AuditViewModel: ObservableObject {
    @Published var events: [AuditEvent] = []
    @Published var filterAction: String? = nil
    @Published var filterBattery: Int? = nil
    @Published var pendingSyncCount: Int = 0

    private let auditUseCase: AuditUseCase

    init() {
        self.auditUseCase = SharedFactory.companion.createAuditUseCase()
        reload()
    }

    func reload() {
        auditUseCase.getEvents(
            action: filterAction,
            batteryIndex: filterBattery.map { Int32($0) }
        ) { [weak self] result in
            DispatchQueue.main.async {
                self?.events = result
            }
        }
        pendingSyncCount = Int(auditUseCase.getPendingSyncCount())
    }

    func clearFilters() {
        filterAction = nil
        filterBattery = nil
        reload()
    }
}
```

- [ ] **Step 2: Create AuditRowView**

```swift
import SwiftUI
import Shared

struct AuditRowView: View {
    let event: AuditEvent

    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Image(systemName: actionIcon)
                .foregroundColor(actionColor)
                .frame(width: 24)

            VStack(alignment: .leading, spacing: 2) {
                Text(event.action.replacingOccurrences(of: "_", with: " ").capitalized)
                    .font(.subheadline.bold())

                if let target = event.target?.intValue {
                    Text("Batterie \(target + 1)")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }

                if let detail = event.detail {
                    Text(detail)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }

            Spacer()

            VStack(alignment: .trailing, spacing: 2) {
                Text(event.userId)
                    .font(.caption2)
                    .foregroundColor(.secondary)
                Text(formatTimestamp(event.timestamp))
                    .font(.caption2)
                    .foregroundColor(.secondary)
            }
        }
    }

    private var actionIcon: String {
        switch event.action {
        case "switch_on": return "bolt.fill"
        case "switch_off": return "bolt.slash"
        case "reset": return "arrow.counterclockwise"
        case "config_change": return "wrench"
        case "wifi_config": return "wifi"
        default: return "doc"
        }
    }

    private var actionColor: Color {
        switch event.action {
        case "switch_on": return .green
        case "switch_off": return .red
        case "config_change", "wifi_config": return .blue
        default: return .secondary
        }
    }

    private func formatTimestamp(_ ms: Int64) -> String {
        let date = Date(timeIntervalSince1970: Double(ms) / 1000.0)
        let fmt = DateFormatter()
        fmt.dateFormat = "dd/MM HH:mm:ss"
        return fmt.string(from: date)
    }
}
```

- [ ] **Step 3: Create AuditView**

```swift
import SwiftUI
import Shared

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
```

- [ ] **Step 4: Build to verify**

```bash
xcodebuild -workspace KXKMBmu.xcworkspace -scheme KXKMBmu -sdk iphonesimulator build 2>&1 | tail -3
```

- [ ] **Step 5: Commit**

```bash
git add iosApp/KXKMBmu/ViewModels/AuditViewModel.swift \
        iosApp/KXKMBmu/Views/Audit/
git commit -m "feat(ios): audit trail with filters and sync indicator"
```

---

### Task 9: Config screen — protection + WiFi + users + sync + transport

**Files:**
- Create: `iosApp/KXKMBmu/ViewModels/ConfigViewModel.swift`
- Create: `iosApp/KXKMBmu/Views/Config/ConfigView.swift`
- Create: `iosApp/KXKMBmu/Views/Config/ProtectionConfigView.swift`
- Create: `iosApp/KXKMBmu/Views/Config/WifiConfigView.swift`
- Create: `iosApp/KXKMBmu/Views/Config/UserManagementView.swift`
- Create: `iosApp/KXKMBmu/Views/Config/SyncConfigView.swift`
- Create: `iosApp/KXKMBmu/Views/Config/TransportConfigView.swift`

- [ ] **Step 1: Create ConfigViewModel**

```swift
import SwiftUI
import Shared

class ConfigViewModel: ObservableObject {
    // Protection
    @Published var minMv: Int = 24000
    @Published var maxMv: Int = 30000
    @Published var maxMa: Int = 10000
    @Published var diffMv: Int = 1000

    // WiFi
    @Published var wifiSsid: String = ""
    @Published var wifiPassword: String = ""
    @Published var wifiStatus: WifiStatusInfo? = nil

    // Users
    @Published var users: [UserProfile] = []

    // Sync
    @Published var syncUrl: String = ""
    @Published var mqttBroker: String = ""
    @Published var syncPending: Int = 0
    @Published var lastSyncTime: Date? = nil

    // Transport
    @Published var activeChannel: TransportChannel = .offline
    @Published var forceChannel: TransportChannel? = nil

    @Published var statusMessage: String? = nil

    private let configUseCase: ConfigUseCase
    private let authUseCase: AuthUseCase

    init() {
        self.configUseCase = SharedFactory.companion.createConfigUseCase()
        self.authUseCase = SharedFactory.companion.createAuthUseCase()
        loadAll()
    }

    func loadAll() {
        let cfg = configUseCase.getCurrentConfig()
        minMv = Int(cfg.minMv)
        maxMv = Int(cfg.maxMv)
        maxMa = Int(cfg.maxMa)
        diffMv = Int(cfg.diffMv)

        users = authUseCase.getAllUsers()
        syncPending = Int(configUseCase.getPendingSyncCount())
    }

    func saveProtection() {
        configUseCase.setProtectionConfig(
            minMv: Int32(minMv), maxMv: Int32(maxMv),
            maxMa: Int32(maxMa), diffMv: Int32(diffMv)
        ) { [weak self] result in
            DispatchQueue.main.async {
                self?.statusMessage = result.isSuccess ? "Seuils mis à jour" : "Erreur"
            }
        }
    }

    func sendWifiConfig() {
        configUseCase.setWifiConfig(ssid: wifiSsid, password: wifiPassword) { [weak self] result in
            DispatchQueue.main.async {
                self?.statusMessage = result.isSuccess ? "WiFi configuré" : "Erreur (BLE requis)"
            }
        }
    }

    func deleteUser(_ user: UserProfile) {
        authUseCase.deleteUser(userId: user.id)
        users = authUseCase.getAllUsers()
    }

    func createUser(name: String, pin: String, role: UserRole) {
        authUseCase.createUser(name: name, pin: pin, role: role)
        users = authUseCase.getAllUsers()
    }
}
```

- [ ] **Step 2: Create ConfigView (root)**

```swift
import SwiftUI

struct ConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        NavigationStack {
            List {
                NavigationLink("Protection") {
                    ProtectionConfigView().environmentObject(vm)
                }
                NavigationLink("WiFi BMU") {
                    WifiConfigView().environmentObject(vm)
                }
                NavigationLink("Utilisateurs") {
                    UserManagementView().environmentObject(vm)
                }
                NavigationLink("Sync cloud") {
                    SyncConfigView().environmentObject(vm)
                }
                NavigationLink("Transport") {
                    TransportConfigView().environmentObject(vm)
                }
            }
            .navigationTitle("Configuration")
        }
    }
}
```

- [ ] **Step 3: Create ProtectionConfigView**

```swift
import SwiftUI

struct ProtectionConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        Form {
            Section("Seuils de tension") {
                Stepper("V min: \(vm.minMv) mV", value: $vm.minMv, in: 20000...30000, step: 500)
                Stepper("V max: \(vm.maxMv) mV", value: $vm.maxMv, in: 25000...35000, step: 500)
                Stepper("V diff max: \(vm.diffMv) mV", value: $vm.diffMv, in: 100...5000, step: 100)
            }
            Section("Seuil de courant") {
                Stepper("I max: \(vm.maxMa) mA", value: $vm.maxMa, in: 1000...50000, step: 1000)
            }
            Section {
                Button("Envoyer au BMU") { vm.saveProtection() }
                    .buttonStyle(.borderedProminent)
            }
            if let msg = vm.statusMessage {
                Section { Text(msg).foregroundColor(.green) }
            }
        }
        .navigationTitle("Protection")
    }
}
```

- [ ] **Step 4: Create WifiConfigView**

```swift
import SwiftUI

struct WifiConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        Form {
            Section("Configuration WiFi du BMU") {
                TextField("SSID", text: $vm.wifiSsid)
                SecureField("Mot de passe", text: $vm.wifiPassword)
            }
            Section {
                Button("Envoyer via BLE") { vm.sendWifiConfig() }
                    .buttonStyle(.borderedProminent)
                Text("La config WiFi est envoyée via BLE uniquement.")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            if let status = vm.wifiStatus {
                Section("État actuel") {
                    HStack {
                        Text("SSID")
                        Spacer()
                        Text(status.ssid).foregroundColor(.secondary)
                    }
                    HStack {
                        Text("IP")
                        Spacer()
                        Text(status.ip).foregroundColor(.secondary)
                    }
                    HStack {
                        Text("RSSI")
                        Spacer()
                        Text("\(status.rssi) dBm").foregroundColor(.secondary)
                    }
                }
            }
        }
        .navigationTitle("WiFi BMU")
    }
}
```

- [ ] **Step 5: Create UserManagementView**

```swift
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
```

- [ ] **Step 6: Create SyncConfigView**

```swift
import SwiftUI

struct SyncConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        Form {
            Section("kxkm-ai") {
                TextField("URL API", text: $vm.syncUrl)
                    .textContentType(.URL)
                    .keyboardType(.URL)
                TextField("Broker MQTT", text: $vm.mqttBroker)
            }
            Section("État") {
                HStack {
                    Text("En attente de sync")
                    Spacer()
                    Text("\(vm.syncPending)")
                        .foregroundColor(vm.syncPending > 0 ? .orange : .green)
                }
                if let last = vm.lastSyncTime {
                    HStack {
                        Text("Dernier sync")
                        Spacer()
                        Text(last, style: .relative).foregroundColor(.secondary)
                    }
                }
            }
        }
        .navigationTitle("Sync cloud")
    }
}
```

- [ ] **Step 7: Create TransportConfigView**

```swift
import SwiftUI
import Shared

struct TransportConfigView: View {
    @EnvironmentObject var vm: ConfigViewModel

    var body: some View {
        Form {
            Section("Canal actif") {
                HStack {
                    Image(systemName: vm.activeChannel.icon)
                    Text(vm.activeChannel.displayName)
                        .font(.body.bold())
                }
            }
            Section("Forcer un canal") {
                Picker("Canal", selection: $vm.forceChannel) {
                    Text("Automatique").tag(nil as TransportChannel?)
                    Text("BLE").tag(TransportChannel.ble as TransportChannel?)
                    Text("WiFi").tag(TransportChannel.wifi as TransportChannel?)
                    Text("Cloud").tag(TransportChannel.mqttCloud as TransportChannel?)
                }
                .pickerStyle(.segmented)

                Text("Auto = BLE > WiFi > Cloud > Offline")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .navigationTitle("Transport")
    }
}
```

- [ ] **Step 8: Build to verify**

```bash
xcodebuild -workspace KXKMBmu.xcworkspace -scheme KXKMBmu -sdk iphonesimulator build 2>&1 | tail -3
```

- [ ] **Step 9: Commit**

```bash
git add iosApp/KXKMBmu/ViewModels/ConfigViewModel.swift \
        iosApp/KXKMBmu/Views/Config/
git commit -m "feat(ios): config screen — protection, WiFi, users, sync, transport"
```

---

### Task 10: Final integration + Simulator test

**Files:** None (verification only)

- [ ] **Step 1: Full build**

```bash
xcodebuild -workspace KXKMBmu.xcworkspace -scheme KXKMBmu -sdk iphonesimulator build 2>&1 | tail -5
```
Expected: `BUILD SUCCEEDED`

- [ ] **Step 2: Run in simulator**

```bash
xcodebuild -workspace KXKMBmu.xcworkspace -scheme KXKMBmu \
    -sdk iphonesimulator -destination 'platform=iOS Simulator,name=iPhone 15' \
    build 2>&1 | tail -5
```

Verify in Simulator:
- Onboarding screen appears (first launch)
- After creating admin → TabView with 4 tabs visible
- Dashboard shows "Aucune batterie détectée" (no BMU connected)
- System screen loads (empty data)
- Config screen navigable with all 5 sub-screens

- [ ] **Step 3: Commit final**

```bash
git add -A
git commit -m "feat(ios): complete iOS SwiftUI app — 5 screens, auth, controls"
```

---

## Dependencies on Plan 2 (KMP Shared)

This plan references these shared module types and factories that Plan 2 must provide:

**Types:** `BatteryState`, `BatteryStatus`, `SystemInfo`, `SolarData`, `AuditEvent`, `UserProfile`, `UserRole`, `TransportChannel`, `WifiStatusInfo`, `BatteryHistoryPoint`

**Factory:** `SharedFactory.companion.create*()` methods for:
- `MonitoringUseCase` — observeBatteries, observeBattery, observeSystem, observeSolar, getHistory
- `ControlUseCase` — switchBattery, resetSwitchCount
- `ConfigUseCase` — getCurrentConfig, setProtectionConfig, setWifiConfig, getPendingSyncCount
- `AuditUseCase` — getEvents, getPendingSyncCount
- `AuthUseCase` — authenticate, createUser, deleteUser, getAllUsers, hasNoUsers
- `TransportManager` — channel state flow

**KMP→Swift bridge:** Callbacks `(result) -> Unit` are used for async operations. Plan 2 should use `suspend` functions with SKIE or manual `FlowCollector` wrappers for Swift consumption.
