# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Native iOS companion app for the KXKM BMU (Battery Management Unit). Connects to ESP32-based BMU hardware via CoreBluetooth to monitor and control parallel battery arrays (24–30V). Part of the larger KXKM Batterie Parallelator project — see parent `../CLAUDE.md` for firmware context.

## Build

Open `KXKMBmu.xcodeproj` in Xcode. The project is a pure SwiftUI iOS app with no external dependencies (no SPM, no CocoaPods). Build target: `KXKMBmu`.

## Architecture

### Dual-mode data: BLE + Stubs

The app has two data paths that coexist:

- **BLE (real)**: `BleManager` (singleton) connects to `KXKM-BMU-*` devices via CoreBluetooth, parses custom GATT characteristics, and publishes real battery/system/solar data via `@Published` properties.
- **Stubs (mock)**: `SharedStubs.swift` provides `MonitoringUseCase`, `ControlUseCase`, `ConfigUseCase`, `AuditUseCase`, `AuthUseCase` — all with mock data. These mirror what will become a KMP (Kotlin Multiplatform) Shared framework. The `SharedFactory` enum is the entry point.

ViewModels observe `BleManager.$batteries` etc. via Combine, with a 3-second fallback to mock data if BLE is not connected. `DeviceSelectorView` also has a "Mode démo" button that forces mock mode.

**KMP migration note**: All stubs have `// import Shared — using Stubs` comments. When the real KMP Shared framework ships, remove `SharedStubs.swift` and uncomment the `import Shared` lines.

### Navigation flow

```
KXKMBmuApp (auto-login admin for dev)
  └─ ContentView
       ├─ StatusBarView (transport + RSSI, always visible)
       ├─ [BLE disconnected] → DeviceSelectorView (scan + demo mode)
       └─ [BLE connected] → TabView
            ├─ DashboardView → BatteryCellView grid → BatteryDetailView
            ├─ SolarView (VE.Direct MPPT data)
            ├─ SystemView (firmware, heap, topology)
            ├─ AuditView (event log)
            └─ ConfigView (admin-only, role-gated)
                 ├─ ProtectionConfigView
                 ├─ WifiConfigView
                 ├─ SyncConfigView
                 ├─ TransportConfigView
                 └─ UserManagementView
```

### BLE GATT protocol

Custom 128-bit UUID base: `4B584B4D-XXXX-4B4D-424D-55424C450000` where `XXXX` is the characteristic suffix.

| Service | Suffix | Characteristics |
|---------|--------|-----------------|
| Battery | 0x0001 | 0x0010–0x001F (up to 16 batteries, 15+ bytes each: V/I/state/Ah/switches) |
| System  | 0x0002 | 0x0020 FW version, 0x0021 heap, 0x0022 uptime, 0x0023 WiFi IP, 0x0024 topology, 0x0025 solar |
| Control | 0x0003 | 0x0030 switch, 0x0031 reset, 0x0032 config, 0x0033 status, 0x0034 wifi cfg, 0x0035 wifi sts |

All multi-byte GATT values are **little-endian**. See `Data` extension helpers in `BleManager.swift`.

### Role-based access

Three roles via `UserRole`: admin (full control + config), technician (control only), viewer (read-only). Battery switch controls and config tab are gated by `canControl` / `canConfigure`. Currently auto-login as admin for development (see `KXKMBmuApp.swift`).

### Key patterns

- **EnvironmentObject**: `AuthViewModel` is injected at app root, other VMs are `@StateObject` in `ContentView`
- **Combine**: ViewModels subscribe to `BleManager` publishers; no async/await for BLE observation
- **Confirmation dialogs**: Battery switch actions use `ConfirmationDialog` ViewModifier (`.confirmAction(...)`)
- **Units**: All internal values in mV/mA/mAh (Int32). Display conversions in `SharedBridge.swift` extensions
- **Dark mode forced**: `.preferredColorScheme(.dark)` on ContentView

## Conventions

- UI labels and comments in **French** (code identifiers in English)
- No tests yet — UI-only project
- Files not yet in Xcode project navigator (SolarCardView, SolarView) exist on disk but may need to be added to the `.xcodeproj`

## Safety-critical rules (inherited from firmware)

- Never weaken protection thresholds sent via BLE config writes
- Battery switch commands require confirmation dialog — never bypass
- Respect role gating (`canControl`, `canConfigure`) on all mutation paths
