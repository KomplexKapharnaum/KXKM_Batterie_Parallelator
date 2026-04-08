# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Native iOS companion app for the KXKM BMU (Battery Management Unit). Connects to ESP32-based BMU hardware via CoreBluetooth to monitor and control parallel battery arrays (24–30V). Part of the larger KXKM Batterie Parallelator project — see parent `../CLAUDE.md` for firmware context.

## Build

Open `KXKMBmu.xcodeproj` in Xcode. The project is a pure SwiftUI iOS app with no external dependencies (no SPM, no CocoaPods). Build target: `KXKMBmu`.

**Important**: Xcode project navigator paths (`KXKMBmu/KXKMBmu/...`) differ from filesystem paths (`KXKMBmu/...`). Use the `Edit`/`Write`/`Read` tools (filesystem) for reliable edits, not `XcodeUpdate` which writes to the Xcode buffer only.

## Architecture

### Production data: BLE only (no mock fallback)

The app runs exclusively on real BLE data from the BMU:

- **BLE (real)**: `BleManager` (singleton) connects to `KXKM-BMU-*` devices via CoreBluetooth, parses custom GATT characteristics, and publishes real battery/system/solar data via `@Published` properties. Includes a 2-second poll timer as workaround for a firmware notification bug.
- **Stubs (type definitions only)**: `SharedStubs.swift` defines model types (`BatteryState`, `SystemInfo`, `SolarData`, etc.) and use-case stubs. These mirror what will become a KMP (Kotlin Multiplatform) Shared framework. The `SharedFactory` enum is the entry point for stubs.

ViewModels observe `BleManager.$batteries` etc. via async/await `for await ... in .values` pattern. No mock fallback — if BLE is disconnected, the dashboard shows a loading spinner with BLE debug log.

**KMP migration note**: When the real KMP Shared framework ships, remove `SharedStubs.swift` and add a bridge layer to map `SharedBatteryState` → `BatteryState`.

### Navigation flow

```
KXKMBmuApp (#if DEBUG: auto-login admin)
  └─ ContentView
       ├─ StatusBarView (transport + RSSI, always visible)
       ├─ [BLE disconnected] → DeviceSelectorView (scan + #if DEBUG demo mode)
       └─ [BLE connected] → TabView
            ├─ DashboardView → BatteryCellView grid → BatteryDetailView
            │    └─ (loading state shows BLE debug log overlay)
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
| Battery | 0x0001 | 0x0010–0x002F (up to 32 batteries, 18 bytes each: V/I/state/Ah/switches) + R_int (0x0038-0x0039) + SOH (0x003A) |
| System  | 0x0002 | 0x0020 FW version, 0x0021 heap, 0x0022 uptime, 0x0023 WiFi IP, 0x0024 topology, 0x0025 solar (15 bytes) |
| Control | 0x0003 | 0x0030 switch, 0x0031 reset, 0x0032 config, 0x0033 status, 0x0034 wifi cfg, 0x0035 wifi sts |
| Victron | 0x68C1… | Victron SmartShunt emulation (separate UUID base, 9 chars) |

**UUID collision warning**: Battery chars 0x0020–0x002F collide with System chars 0x0020–0x0025. The `didUpdateValueFor` handler disambiguates using `characteristic.service?.uuid` (parent service filtering). Never match by UUID alone.

**Battery GATT struct** (18 bytes, packed little-endian):
| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | voltage_mv (int32) |
| 4 | 4 | current_ma (int32) |
| 8 | 1 | state (uint8: 0=connected, 1=disconnected, 2=reconnecting, 3=error, 4=locked) |
| 9 | 4 | ah_discharge_mah (int32) |
| 13 | 4 | ah_charge_mah (int32) |
| 17 | 1 | nb_switch (uint8) |

**Solar GATT struct** (15 bytes, packed little-endian):
| Offset | Size | Field |
|--------|------|-------|
| 0 | 2 | battery_voltage_mv (int16) |
| 2 | 2 | battery_current_ma (int16) |
| 4 | 2 | panel_voltage_mv (uint16) |
| 6 | 2 | panel_power_w (uint16) |
| 8 | 1 | charge_state (uint8) |
| 9 | 1 | error_code (uint8) |
| 10 | 4 | yield_today_wh (uint32) |
| 14 | 1 | valid (uint8) |

All multi-byte GATT values are **little-endian**. See `Data` extension helpers in `BleManager.swift`.

### BleManager key mechanisms

- **Service discovery**: Uses `discoverServices(nil)` (all services) to avoid CoreBluetooth UUID cache issues with 128-bit customs
- **Poll timer**: 2-second periodic re-read of battery + system characteristics. Workaround for firmware `ble_gatts_notify_custom(0xFFFF)` bug where notifications don't reach iOS
- **Topology-aware polling**: After receiving topology (INA count), trims `batteryChars` dictionary to only poll valid battery indices. Defers battery reads until after topology is received
- **Debug log**: `@Published bleDebugLog: [String]` (40-line circular buffer) shown in DashboardView loading state for live debugging
- **`logBle(_:)`**: Dual-writes to `os.log` (Xcode console) and `bleDebugLog` (UI)

### Role-based access

Three roles via `UserRole`: admin (full control + config), technician (control only), viewer (read-only). Battery switch controls and config tab are gated by `canControl` / `canConfigure`. Auto-login as admin in `#if DEBUG` only.

### Key patterns

- **EnvironmentObject**: `AuthViewModel` is injected at app root, other VMs are `@StateObject` in `ContentView`
- **Async/await observation**: ViewModels use `for await value in BleManager.shared.$property.values` in `Task` objects (stored in `observeTasks: [Task<Void, Never>]`, cancelled in `deinit`)
- **@MainActor**: All ViewModels annotated `@MainActor`
- **Confirmation dialogs**: Battery switch actions use `ConfirmationDialog` ViewModifier (`.confirmAction(...)`)
- **Units**: All internal values in mV/mA/mAh (Int32). Display conversions in `SharedBridge.swift` extensions
- **Dark mode forced**: `.preferredColorScheme(.dark)` on ContentView

## Conventions

- UI labels and comments in **French** (code identifiers in English)
- No tests yet — UI-only project
- `#if DEBUG` gates: auto-login admin, demo mode button

## Known firmware issues

- **`ble_gatts_notify_custom(0xFFFF)` doesn't work**: NimBLE requires a real `conn_handle`, not 0xFFFF. iOS workaround: poll timer re-reads every 2s
- **`s_nb_ina` stale at init**: BLE initialized before I2C scan → `s_nb_ina=0`. Fixed in `bmu_ble_battery_svc.cpp` to read `prot->nb_ina` dynamically (pending firmware reflash)

## Safety-critical rules (inherited from firmware)

- Never weaken protection thresholds sent via BLE config writes
- Battery switch commands require confirmation dialog — never bypass
- Respect role gating (`canControl`, `canConfigure`) on all mutation paths
