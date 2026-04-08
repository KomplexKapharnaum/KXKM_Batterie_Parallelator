# iOS ↔ Firmware Coherence — Design Spec

## Goal

Achieve 100% feature parity between the ESP-IDF firmware GATT services and the iOS companion app. Every firmware feature exposed via BLE must have a corresponding iOS UI. Eliminate dead code, fix documentation, and add missing GATT characteristics.

## Scope

6 work streams covering both firmware (new GATT chars) and iOS (new views + parsing):

1. Battery Labels — config UI for per-battery names
2. R_int Resistance — display + on-demand trigger
3. SOH Estimation — health gauge + dashboard badge
4. Balancer State — new GATT char + iOS indicator
5. Victron Scan Results — new GATT char + system view section
6. Cleanup — comments, dead logs, CLAUDE.md docs

## Current State (Audit 2026-04-08)

| Feature | Firmware GATT | iOS App | Gap |
|---------|:------------:|:-------:|-----|
| Battery data (0x0010-0x002F) | 18B packed | Reads + parses | Aligned |
| System info (0x0020-0x0025) | 6 chars | Reads all | Aligned |
| Control commands (0x0030-0x0035) | 6 W/N chars | Sends all | Aligned |
| Battery labels (0x0036) | WRITE only | No UI | **Gap** |
| MQTT config (0x0037) | R/W | R/W UI | Aligned |
| Device name (0x0038) | R/W | R/W UI | Aligned |
| R_int (0x0038-0x0039 battery svc) | READ optional | Ignored | **Gap** |
| SOH (0x003A battery svc) | READ optional | Ignored | **Gap** |
| Balancer state | Not exposed | No code | **Gap** |
| Victron scan results | Not exposed | Ignored | **Gap** |
| Victron SmartShunt (68C10001) | Full service | Logs "Unknown" | **Noise** |

---

## 1. Battery Labels

### Firmware (existing, minor addition)

Char 0x0036 in Control Service already accepts WRITE (index + label UTF-8, max 10 bytes). Add READ support: when read, return all labels packed as `[nb_labels(1)] + [label_0(9)] + [label_1(9)] + ...` (up to nb_ina labels, 9 bytes each null-padded). Max payload = 1 + 32*9 = 289 bytes — may need chunked reads or limit to first 16.

Simpler alternative: READ returns label for battery index written in first byte of the read request. iOS reads each label sequentially after topology tells it nb_ina.

### iOS

New `BatteryLabelsView.swift` in Config section:
- List of batteries (1..nb_ina) with current label
- Tap → text field (max 8 chars), "Envoyer" button
- Sends 0x0036 WRITE: `[index(1)] + [label UTF-8]`
- On connect: read labels for all batteries sequentially (0x0036 READ with index byte)

### BleManager additions
- `setBatteryLabel(index: Int, label: String)` — WRITE to 0x0036
- Parse label READ responses, store in `@Published var batteryLabels: [Int: String]`

---

## 2. R_int Resistance

### Firmware (existing)

Char 0x0038 (battery service, CONFIG_BMU_RINT_ENABLED):
- WRITE 1 byte: battery index → triggers on-demand R_int measurement
- Char 0x0039: READ returns per-battery results

R_int result struct (11 bytes per battery, packed LE):
```
[0:2]  r_ohmic_mohm (uint16)
[2:4]  r_total_mohm (uint16)
[4:6]  v_load_mv (uint16)
[6:8]  v_ocv_fast_mv (uint16)
[8:10] i_load_ma (uint16)
[10]   valid (uint8)
```

### iOS

In `BatteryDetailView`:
- Section "Resistance interne" below counters
- Display: `R₀ = 12.3 mΩ` (color: green ≤50, orange ≤100, red >100)
- Display: `R_total = 18.7 mΩ`
- Button "Mesurer R_int" → writes battery index to 0x0038
- Refreshes after 2s (R_int measurement takes ~1s)

### BleManager additions
- `kRintTriggerUUID = bmuUUID(0x0038)` (in battery service context)
- `kRintResultUUID = bmuUUID(0x0039)`
- `triggerRintMeasurement(index: Int)` — WRITE index to 0x0038
- Parse 0x0039 READ: extract r_ohmic, r_total, valid per battery
- `@Published var rintResults: [Int: RintResult]`

New model in SharedStubs:
```swift
struct RintResult {
    let rOhmicMohm: Float
    let rTotalMohm: Float
    let valid: Bool
}
```

**UUID collision note**: 0x0038 is used by both Control Service (device name) and Battery Service (R_int trigger). BleManager must use parent service UUID to disambiguate — already handled by the `parentSvcUUID` filtering pattern.

---

## 3. SOH Estimation

### Firmware (existing)

Char 0x003A (battery service, CONFIG_BMU_BLE_SOH_ENABLED):
- READ: returns per-battery SOH data

SOH struct (7 bytes per battery, packed LE):
```
[0:2]  soh_percent (uint16, 0-10000 = 0-100.00%)
[2:4]  confidence (uint16, 0-10000)
[4:6]  rint_mohm (uint16, last R_int ohmic)
[6]    rint_valid (uint8)
```

### iOS

In `BatteryDetailView`:
- Circular gauge showing SOH% (green ≥70%, orange ≥40%, red <40%)
- Confidence indicator below (e.g. "Confiance: 85%")

In `BatteryCellView` (dashboard grid):
- Small colored badge in corner if SOH < 70% (orange) or < 40% (red)
- No badge if SOH ≥ 70% or data unavailable

### BleManager additions
- `kSohUUID = bmuUUID(0x003A)`
- Parse: loop nb_ina batteries, extract 7 bytes each
- `@Published var sohResults: [Int: SohResult]`

New model:
```swift
struct SohResult {
    let sohPercent: Float    // 0-100
    let confidence: Float    // 0-100
    let rintMohm: Float
    let rintValid: Bool
}
```

---

## 4. Balancer State

### Firmware (new GATT char)

New char 0x003B in Battery Service:
- READ + NOTIFY (1s, piggyback on battery timer)
- Payload: `[nb_batteries(1)] + [per_battery(2)] × nb_ina`
- Per-battery (2 bytes): `[flags(1)] + [duty_pct(1)]`
  - flags bit 0: is_off (currently in OFF duty phase)
  - flags bit 1: balancing (this battery is being duty-cycled)
  - duty_pct: 0-100 effective duty percentage

Implementation in `bmu_ble_battery_svc.cpp`:
- New static array `s_bal_data[BMU_MAX_BATTERIES * 2 + 1]`
- Build from `bmu_balancer_is_off(i)` and `bmu_balancer_get_duty_pct(i)`
- Notify via existing battery timer (1s)

### iOS

In `BatteryCellView`:
- If `balancing == true`: orange "⚖" icon overlay
- If `is_off == true`: cell border pulsing orange

In `BatteryDetailView`:
- Section "Equilibrage": "Duty: 60% (3 ON / 2 OFF)" or "Pas d'equilibrage"
- Color: orange if active, gray if not

### BleManager additions
- `kBalancerUUID = bmuUUID(0x003B)`
- Parse: 1 byte count + 2 bytes per battery
- `@Published var balancerState: [Int: BalancerInfo]`

New model:
```swift
struct BalancerInfo {
    let isOff: Bool
    let balancing: Bool
    let dutyPct: Int
}
```

---

## 5. Victron Scan Results

### Firmware (new GATT char)

New char 0x003C in System Service:
- READ (polled by iOS every 10s with system chars)
- Payload: `[nb_devices(1)] + per_device × nb_devices`
- Per device (19 bytes):
  ```
  [0]     record_type (uint8: 0x01=solar, 0x02=battery, 0x03=inverter, 0x04=dcdc)
  [1:7]   mac (6 bytes)
  [7]     decrypted (uint8: 0=locked, 1=ok)
  [8:18]  data (10 bytes, raw decrypted payload)
  [18]    rssi (int8)
  ```
- Max 4 devices = 1 + 4*19 = 77 bytes

Implementation in `bmu_ble_system_svc.cpp`:
- Read from `bmu_vic_scan_get_devices()` (already exists in bmu_ble_victron_scan)
- Pack into static buffer, return on READ

### iOS

In `SystemView`:
- Section "Victron" (after WiFi/BLE info)
- Per device row: icon (solar/battery/inverter) + label or MAC + key values
  - Solar: `MPPT  28.5V 350W Bulk`
  - Battery: `Shunt 27.2V -3.2A 98%`
  - Locked: `A4:C1:38 (locked)`
- Tap on device → detail (future: key config)

### BleManager additions
- `kVictronScanUUID = bmuUUID(0x0026)` (next free system char)
- Parse 19 bytes per device
- `@Published var victronDevices: [VictronDevice]`

New model:
```swift
struct VictronDevice: Identifiable {
    let id: String  // MAC hex
    let recordType: UInt8
    let mac: [UInt8]
    let decrypted: Bool
    let data: Data  // 10 bytes raw
    let rssi: Int
}
```

---

## 6. Cleanup

### Firmware
- Fix comment in `bmu_ble_battery_svc.cpp` line 30: "15 octets" → "18 bytes (packed)"
- Add `bmu_balancer.h` include in battery svc for balancer state char

### iOS
- In BleManager `didUpdateValueFor`: replace `Unknown char 68C100xx` log with `// Victron SmartShunt service — handled by VictronConnect`
- Add Victron service UUID constant `kVictronSvcUUID` and skip silently

### Documentation
- Update `iosApp/CLAUDE.md` GATT table with chars 0x0036-0x003C
- Update root `CLAUDE.md` BLE section with full char list

---

## GATT Map (complete after implementation)

### Battery Service (0x0001)
| Char | UUID Suffix | Type | Payload | Notes |
|------|:-----------:|------|---------|-------|
| Battery 0-31 | 0x0010-0x002F | R/N | 18B packed | V, I, state, Ah, switches |
| R_int trigger | 0x0038 | W | 1B index | On-demand measurement |
| R_int results | 0x0039 | R/N | 11B × nb_ina | R_ohmic, R_total, valid |
| SOH | 0x003A | R/N | 7B × nb_ina | SOH%, confidence, R_int |
| Balancer | 0x003B | R/N | 1 + 2B × nb_ina | is_off, duty_pct |

### System Service (0x0002)
| Char | UUID Suffix | Type | Payload | Notes |
|------|:-----------:|------|---------|-------|
| FW version | 0x0020 | R | string | |
| Heap free | 0x0021 | R/N | 4B uint32 | 10s poll |
| Uptime | 0x0022 | R | 4B uint32 | seconds |
| WiFi IP | 0x0023 | R | string | |
| Topology | 0x0024 | R/N | 3B | nb_ina, nb_tca, valid |
| Solar | 0x0025 | R/N | 15B packed | VE.Direct data |
| Victron scan | 0x0026 | R | 1 + 19B × n | Scanned Victron devices |

### Control Service (0x0003)
| Char | UUID Suffix | Type | Payload | Notes |
|------|:-----------:|------|---------|-------|
| Switch | 0x0030 | W | 2B | index + on/off |
| Reset | 0x0031 | W | 1B | index |
| Config | 0x0032 | W | 8B | thresholds (LE uint16 ×4) |
| Status | 0x0033 | R/N | 3B | last_cmd, idx, result |
| WiFi cfg | 0x0034 | W | 96B | ssid(32)+pass(64) |
| WiFi status | 0x0035 | R/N | 50B | ssid+ip+rssi+connected |
| Battery label | 0x0036 | R/W | 1+8B | index + label UTF-8 |
| MQTT cfg | 0x0037 | R/W | 82B W/48B R | uri+user+pass |
| Device name | 0x0038 | R/W | 32B | UTF-8 null-padded |

### Victron SmartShunt Service (68C10001-b17f-4d3a-a290-34ad6499937c)
| Char | UUID Suffix | Type | Notes |
|------|:-----------:|------|-------|
| Voltage | 0x0011 | R/N | 0.01V uint16 |
| Current | 0x0012 | R/N | 0.1A int16 |
| SOC | 0x0013 | R/N | 0-10000 uint16 |
| Consumed Ah | 0x0014 | R/N | 0.1Ah int32 |
| TTG | 0x0015 | R/N | minutes uint16 |
| Temperature | 0x0016 | R/N | 0.01K int16 |
| Alarm | 0x0017 | R/N | bitmask uint16 |
| Model | 0x0020 | R | "KXKM BMU" |
| Serial | 0x0021 | R | device name |

---

## Safety

- R_int trigger (0x0038 WRITE) requires minimum 2 connected batteries (firmware guard)
- Balancer never touches ERROR/LOCKED batteries (firmware safety)
- Battery label max 8 chars enforced firmware-side
- All new chars follow existing auth pattern (WRITE_ENC for mutations)

## Testing

- Firmware: build verification (`idf.py build`)
- iOS: Xcode build + BLE read verification via Bleak from Mac
- Integration: connect app to BMU, verify all new sections display data
