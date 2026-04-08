# iOS ↔ Firmware Coherence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Achieve 100% GATT feature parity between ESP-IDF firmware and iOS app — every firmware BLE feature gets an iOS UI.

**Architecture:** 3 phases: (1) firmware adds new GATT chars (balancer 0x003B, Victron scan 0x0026, battery label READ), (2) iOS parses all existing+new chars and adds UI views, (3) cleanup (comments, logs, docs). Each task is independently buildable and testable.

**Tech Stack:** ESP-IDF 5.4 (NimBLE GATT), Swift/SwiftUI (CoreBluetooth), LVGL

---

## Phase 1: Firmware — New GATT Characteristics

### Task 1: Balancer state char (0x003B) in Battery Service

**Files:**
- Modify: `firmware-idf/components/bmu_ble/bmu_ble_battery_svc.cpp`

- [ ] **Step 1: Add UUID + handle + struct**

After the SOH declarations (~line 111), add:

```cpp
/* ── Balancer state characteristic 0x003B ─────────────────────────── */
#include "bmu_balancer.h"

typedef struct __attribute__((packed)) {
    uint8_t flags;      /* bit0=is_off, bit1=balancing */
    uint8_t duty_pct;   /* 0-100 effective duty */
} ble_balancer_char_t;  /* 2 bytes per battery */

static ble_uuid128_t s_bal_state_uuid = BMU_BLE_UUID128_DECLARE(0x3B, 0x00);
static uint16_t s_bal_val_handle = 0;
```

- [ ] **Step 2: Add read callback**

After the SOH read callback, add:

```cpp
static int bal_state_access_cb(uint16_t conn, uint16_t attr,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;

    bmu_protection_ctx_t *prot = bmu_ble_get_prot();
    uint8_t nb = prot ? prot->nb_ina : bmu_ble_get_nb_ina();
    uint8_t count = nb > 32 ? 32 : nb;
    os_mbuf_append(ctxt->om, &count, 1);

    for (int i = 0; i < count; i++) {
        ble_balancer_char_t bal;
        bal.flags = 0;
        if (bmu_balancer_is_off((uint8_t)i))  bal.flags |= 0x01;
        if (bmu_balancer_get_duty_pct((uint8_t)i) < 100) bal.flags |= 0x02;
        bal.duty_pct = (uint8_t)bmu_balancer_get_duty_pct((uint8_t)i);
        os_mbuf_append(ctxt->om, &bal, sizeof(bal));
    }
    return 0;
}
```

- [ ] **Step 3: Register in s_bat_chr_defs[]**

Increase array size from `BMU_MAX_BATTERIES + 4` to `BMU_MAX_BATTERIES + 5`. After the SOH entry, add:

```cpp
    /* Balancer state 0x003B */
    int bal_base = soh_base + 1;
    s_bat_chr_defs[bal_base].uuid       = &s_bal_state_uuid.u;
    s_bat_chr_defs[bal_base].access_cb  = bal_state_access_cb;
    s_bat_chr_defs[bal_base].arg        = NULL;
    s_bat_chr_defs[bal_base].flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
    s_bat_chr_defs[bal_base].val_handle = &s_bal_val_handle;

    memset(&s_bat_chr_defs[bal_base + 1], 0, sizeof(struct ble_gatt_chr_def));
```

Update the terminator `memset` line to use `bal_base + 1` instead of previous index.

- [ ] **Step 4: Add notify in battery timer**

In `notify_timer_cb`, after the SOH notify block, add:

```cpp
    /* Balancer state notify */
    if (s_bal_val_handle != 0) {
        struct os_mbuf *om_bal = ble_hs_mbuf_from_flat(NULL, 0);
        if (om_bal) {
            uint8_t nb_bal = nb > 32 ? 32 : nb;
            os_mbuf_append(om_bal, &nb_bal, 1);
            for (int i = 0; i < nb_bal; i++) {
                ble_balancer_char_t b;
                b.flags = 0;
                if (bmu_balancer_is_off((uint8_t)i)) b.flags |= 0x01;
                if (bmu_balancer_get_duty_pct((uint8_t)i) < 100) b.flags |= 0x02;
                b.duty_pct = (uint8_t)bmu_balancer_get_duty_pct((uint8_t)i);
                os_mbuf_append(om_bal, &b, sizeof(b));
            }
            ble_gatts_notify_custom(0xFFFF, s_bal_val_handle, om_bal);
        }
    }
```

- [ ] **Step 5: Add bmu_balancer to CMakeLists REQUIRES**

In `firmware-idf/components/bmu_ble/CMakeLists.txt`, add `bmu_balancer` to REQUIRES.

- [ ] **Step 6: Build**

```bash
source ~/esp/esp-idf/export.sh && cd firmware-idf && idf.py build 2>&1 | tail -5
```

- [ ] **Step 7: Commit**

```bash
git add firmware-idf/components/bmu_ble/
git commit -m "feat(ble): balancer state char 0x003B"
```

---

### Task 2: Victron scan results char (0x0026) in System Service

**Files:**
- Modify: `firmware-idf/components/bmu_ble/bmu_ble_system_svc.cpp`
- Modify: `firmware-idf/components/bmu_ble/CMakeLists.txt`

- [ ] **Step 1: Add UUID + handle + struct + enum**

```cpp
#include "bmu_ble_victron_scan.h"

/* Victron scan result — 19 bytes per device */
typedef struct __attribute__((packed)) {
    uint8_t  record_type;  /* 0x01=solar, 0x02=battery, 0x03=inverter, 0x04=dcdc */
    uint8_t  mac[6];
    uint8_t  decrypted;    /* 0=locked, 1=ok */
    uint8_t  data[10];     /* raw decrypted payload */
    int8_t   rssi;
} ble_vic_scan_entry_t;    /* 19 bytes */

static ble_uuid128_t s_vic_scan_chr_uuid = BMU_BLE_UUID128_DECLARE(0x26, 0x00);
static uint16_t s_vic_scan_val_handle = 0;
```

Add to enum: `SYS_CHR_VIC_SCAN` after `SYS_CHR_SOLAR`.

- [ ] **Step 2: Add read case in system_chr_access_cb**

```cpp
case SYS_CHR_VIC_SCAN: {
    bmu_vic_device_t devs[4];
    int n = bmu_vic_scan_get_devices(devs, 4);
    uint8_t count = (uint8_t)n;
    os_mbuf_append(ctxt->om, &count, 1);
    for (int i = 0; i < n; i++) {
        ble_vic_scan_entry_t entry;
        entry.record_type = devs[i].record_type;
        memcpy(entry.mac, devs[i].mac, 6);
        entry.decrypted = devs[i].decrypted ? 1 : 0;
        memcpy(entry.data, devs[i].raw_decrypted, 10);
        entry.rssi = 0; /* TODO: add RSSI to bmu_vic_device_t */
        os_mbuf_append(ctxt->om, &entry, sizeof(entry));
    }
    rc = 0;
    break;
}
```

- [ ] **Step 3: Add to s_sys_chr_defs[] array**

Follow the solar pattern — add before the terminator:
```cpp
{
    .uuid       = &s_vic_scan_chr_uuid.u,
    .access_cb  = system_chr_access_cb,
    .arg        = (void *)(intptr_t)SYS_CHR_VIC_SCAN,
    .flags      = BLE_GATT_CHR_F_READ,
    .val_handle = &s_vic_scan_val_handle,
},
```

- [ ] **Step 4: Add bmu_ble_victron_scan to CMakeLists**

- [ ] **Step 5: Build + commit**

```bash
idf.py build && git add firmware-idf/components/bmu_ble/ && git commit -m "feat(ble): Victron scan char 0x0026"
```

---

### Task 3: Battery label READ support (0x0036)

**Files:**
- Modify: `firmware-idf/components/bmu_ble/bmu_ble_control_svc.cpp`

- [ ] **Step 1: Add READ handler for battery label**

In the `control_chr_access_cb`, before the WRITE gate, add a READ case for `CTRL_CHR_BAT_LABEL`:

```cpp
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (chr_id == CTRL_CHR_BAT_LABEL) {
            /* Return all labels: [nb(1)] + [label(9)] × nb */
            uint8_t nb = bmu_ble_get_nb_ina();
            if (nb > 32) nb = 32;
            os_mbuf_append(ctxt->om, &nb, 1);
            for (int i = 0; i < nb; i++) {
                char lbl[9] = {};
                strncpy(lbl, bmu_config_get_battery_label(i), 8);
                os_mbuf_append(ctxt->om, lbl, 9);
            }
            return 0;
        }
        /* existing READ handlers... */
    }
```

- [ ] **Step 2: Update flags to add READ**

Change the 0x0036 entry flags from `WRITE | WRITE_ENC` to `READ | WRITE | WRITE_ENC`. Add a val_handle.

- [ ] **Step 3: Build + commit**

```bash
idf.py build && git add firmware-idf/components/bmu_ble/ && git commit -m "feat(ble): battery label READ 0x0036"
```

---

## Phase 2: iOS — Parse + UI

### Task 4: iOS — R_int + SOH + Balancer parsing in BleManager

**Files:**
- Modify: `iosApp/KXKMBmu/Stubs/BleManager.swift`
- Modify: `iosApp/KXKMBmu/Stubs/SharedStubs.swift`

- [ ] **Step 1: Add models to SharedStubs.swift**

```swift
struct RintResult {
    let rOhmicMohm: Float   // mΩ
    let rTotalMohm: Float   // mΩ
    let valid: Bool
}

struct SohResult {
    let sohPercent: Float    // 0-100
    let confidence: Float    // 0-100
    let rintMohm: Float
    let rintValid: Bool
}

struct BalancerInfo {
    let isOff: Bool
    let balancing: Bool
    let dutyPct: Int
}

struct VictronDevice: Identifiable {
    let id: String  // MAC hex
    let recordType: UInt8
    let mac: Data
    let decrypted: Bool
    let rawData: Data
}
```

- [ ] **Step 2: Add UUID constants + @Published in BleManager**

```swift
// Battery service extras
private let kRintTriggerUUID = bmuUUID(0x0038) // in battery svc context
private let kRintResultUUID  = bmuUUID(0x0039)
private let kSohUUID         = bmuUUID(0x003A)
private let kBalancerUUID    = bmuUUID(0x003B)

// System service extras
private let kVictronScanUUID = bmuUUID(0x0026)

// Battery label
private let kBatLabelUUID    = bmuUUID(0x0036)

// Published state
@Published var rintResults: [Int: RintResult] = [:]
@Published var sohResults: [Int: SohResult] = [:]
@Published var balancerState: [Int: BalancerInfo] = [:]
@Published var victronDevices: [VictronDevice] = []
@Published var batteryLabels: [Int: String] = [:]
```

- [ ] **Step 3: Add parsing in didUpdateValueFor — Battery service**

After the battery char loop (before `return`), add:

```swift
    // R_int results (0x0039): 11 bytes per battery
    if uuid == kRintResultUUID && data.count >= 12 {
        let nb = Int(data[0])
        for i in 0..<min(nb, (data.count - 1) / 11) {
            let off = 1 + i * 11
            guard off + 11 <= data.count else { break }
            let rOhmic = Float(data.readUInt16LE(at: off)) / 10.0
            let rTotal = Float(data.readUInt16LE(at: off + 2)) / 10.0
            let valid = data[off + 10] != 0
            rintResults[i] = RintResult(rOhmicMohm: rOhmic, rTotalMohm: rTotal, valid: valid)
        }
        return
    }

    // SOH (0x003A): 7 bytes per battery
    if uuid == kSohUUID && data.count >= 8 {
        let nb = Int(data[0])
        for i in 0..<min(nb, (data.count - 1) / 7) {
            let off = 1 + i * 7
            guard off + 7 <= data.count else { break }
            let soh = Float(data[off]) // 0-100
            let conf = Float(data[off + 6]) // 0-100
            let rint = Float(data.readUInt16LE(at: off + 1)) / 10.0
            let rv = data[off + 5] != 0
            sohResults[i] = SohResult(sohPercent: soh, confidence: conf, rintMohm: rint, rintValid: rv)
        }
        return
    }

    // Balancer state (0x003B): 2 bytes per battery
    if uuid == kBalancerUUID && data.count >= 3 {
        let nb = Int(data[0])
        for i in 0..<min(nb, (data.count - 1) / 2) {
            let off = 1 + i * 2
            let flags = data[off]
            let duty = Int(data[off + 1])
            balancerState[i] = BalancerInfo(
                isOff: (flags & 0x01) != 0,
                balancing: (flags & 0x02) != 0,
                dutyPct: duty
            )
        }
        return
    }
```

- [ ] **Step 4: Add parsing in System service switch**

```swift
    case kVictronScanUUID:
        if data.count >= 1 {
            let nb = Int(data[0])
            var devs: [VictronDevice] = []
            for i in 0..<min(nb, (data.count - 1) / 19) {
                let off = 1 + i * 19
                guard off + 19 <= data.count else { break }
                let rt = data[off]
                let mac = data.subdata(in: (off+1)..<(off+7))
                let dec = data[off + 7] != 0
                let raw = data.subdata(in: (off+8)..<(off+18))
                let macHex = mac.map { String(format: "%02X", $0) }.joined(separator: ":")
                devs.append(VictronDevice(id: macHex, recordType: rt, mac: mac, decrypted: dec, rawData: raw))
            }
            victronDevices = devs
            logBle("Victron scan: \(devs.count) devices")
        }
```

- [ ] **Step 5: Add parsing in Control service for labels**

```swift
    } else if uuid == kBatLabelUUID && data.count >= 10 {
        let nb = Int(data[0])
        for i in 0..<min(nb, (data.count - 1) / 9) {
            let off = 1 + i * 9
            let labelData = data.subdata(in: off..<min(off+9, data.count))
            let label = String(data: labelData, encoding: .utf8)?
                .replacingOccurrences(of: "\0", with: "")
                .trimmingCharacters(in: .controlCharacters) ?? "B\(i+1)"
            batteryLabels[i] = label
        }
        logBle("Labels: \(batteryLabels)")
    }
```

- [ ] **Step 6: Add R_int trigger + label set methods**

```swift
    func triggerRintMeasurement(index: Int) {
        guard let char = controlChars[kRintTriggerUUID] ?? batteryChars.values.first(where: { _ in true }) else { return }
        // Write to battery service R_int trigger
        // Need to store the char ref during discovery
        logBle("R_int trigger not yet wired to char discovery")
    }

    func setBatteryLabel(index: Int, label: String) {
        guard let char = controlChars[kBatLabelUUID] else {
            logBle("Battery label char not found")
            return
        }
        var buf = Data(count: 10)
        buf[0] = UInt8(index)
        if let labelData = label.prefix(8).data(using: .utf8) {
            buf.replaceSubrange(1..<(1+min(labelData.count, 8)), with: labelData)
        }
        peripheral?.writeValue(buf, for: char, type: .withResponse)
        logBle("Label[\(index)] = '\(label)'")
    }
```

- [ ] **Step 7: Read labels + Victron on connect**

In `pollAllCharacteristics()`, add reads for kBatLabelUUID and kVictronScanUUID from systemChars/controlChars.

- [ ] **Step 8: Build iOS + commit**

```bash
cd iosApp && xcodebuild build -scheme KXKMBmu -destination "platform=iOS,id=AC1CDE5F-DDC4-5B2C-80D7-9953D8ABA0AC" -allowProvisioningUpdates CODE_SIGN_IDENTITY="Apple Development" 2>&1 | grep BUILD
git add iosApp/ && git commit -m "feat(ios): parse R_int + SOH + balancer + Victron + labels"
```

---

### Task 5: iOS — BatteryDetailView R_int + SOH sections

**Files:**
- Modify: `iosApp/KXKMBmu/Views/Detail/BatteryDetailView.swift`

- [ ] **Step 1: Add R_int section**

After the counters section, add:

```swift
// R_int section
if let rint = bleManager.rintResults[batteryIndex], rint.valid {
    Section("Resistance interne") {
        HStack {
            Text("R ohm")
            Spacer()
            Text(String(format: "%.1f mΩ", rint.rOhmicMohm))
                .foregroundColor(rint.rOhmicMohm <= 50 ? .green : rint.rOhmicMohm <= 100 ? .orange : .red)
                .monospacedDigit()
        }
        HStack {
            Text("R total")
            Spacer()
            Text(String(format: "%.1f mΩ", rint.rTotalMohm))
                .monospacedDigit()
        }
    }
}
```

- [ ] **Step 2: Add SOH gauge**

```swift
if let soh = bleManager.sohResults[batteryIndex] {
    Section("Sante batterie") {
        HStack {
            Text("SOH")
            Spacer()
            Text("\(Int(soh.sohPercent))%")
                .font(.title2.bold())
                .foregroundColor(soh.sohPercent >= 70 ? .green : soh.sohPercent >= 40 ? .orange : .red)
        }
        if soh.confidence > 0 {
            HStack {
                Text("Confiance")
                Spacer()
                Text("\(Int(soh.confidence))%")
                    .foregroundColor(.secondary)
            }
        }
    }
}
```

- [ ] **Step 3: Add balancer indicator**

```swift
if let bal = bleManager.balancerState[batteryIndex], bal.balancing {
    Section("Equilibrage") {
        HStack {
            Image(systemName: "arrow.triangle.2.circlepath")
                .foregroundColor(.orange)
            Text("Duty: \(bal.dutyPct)%")
            if bal.isOff {
                Text("(OFF)")
                    .foregroundColor(.orange)
            }
        }
    }
}
```

- [ ] **Step 4: Build + commit**

---

### Task 6: iOS — BatteryCellView badges + balancer icon

**Files:**
- Modify: `iosApp/KXKMBmu/Views/Dashboard/BatteryCellView.swift`

- [ ] **Step 1: Add SOH badge**

In the cell overlay, add a small colored circle if SOH < 70%:

```swift
.overlay(alignment: .topTrailing) {
    if let soh = BleManager.shared.sohResults[Int(battery.index)],
       soh.sohPercent < 70 {
        Circle()
            .fill(soh.sohPercent < 40 ? Color.red : Color.orange)
            .frame(width: 10, height: 10)
            .padding(4)
    }
}
```

- [ ] **Step 2: Add balancer icon**

```swift
.overlay(alignment: .topLeading) {
    if let bal = BleManager.shared.balancerState[Int(battery.index)],
       bal.balancing {
        Image(systemName: "arrow.triangle.2.circlepath")
            .font(.caption2)
            .foregroundColor(.orange)
            .padding(4)
    }
}
```

- [ ] **Step 3: Build + commit**

---

### Task 7: iOS — Battery Labels Config View

**Files:**
- Create: `iosApp/KXKMBmu/Views/Config/BatteryLabelsView.swift`
- Modify: `iosApp/KXKMBmu/Views/Config/ConfigView.swift`
- Modify: `iosApp/KXKMBmu.xcodeproj/project.pbxproj`

- [ ] **Step 1: Create BatteryLabelsView**

```swift
import SwiftUI

struct BatteryLabelsView: View {
    @State private var labels: [String] = []
    @State private var editingIndex: Int? = nil
    @State private var editText: String = ""

    private let bleManager = BleManager.shared

    var body: some View {
        List {
            let nb = bleManager.systemInfo?.nbIna ?? 4
            ForEach(0..<nb, id: \.self) { i in
                HStack {
                    Text("Bat \(i + 1)")
                        .foregroundColor(.secondary)
                    Spacer()
                    Text(bleManager.batteryLabels[i] ?? "B\(i+1)")
                        .font(.body.bold())
                    Button(action: {
                        editingIndex = i
                        editText = bleManager.batteryLabels[i] ?? ""
                    }) {
                        Image(systemName: "pencil")
                    }
                }
            }
        }
        .navigationTitle("Noms batteries")
        .alert("Renommer", isPresented: Binding(
            get: { editingIndex != nil },
            set: { if !$0 { editingIndex = nil } }
        )) {
            TextField("Nom (max 8 car.)", text: $editText)
            Button("Envoyer") {
                if let idx = editingIndex {
                    bleManager.setBatteryLabel(index: idx, label: String(editText.prefix(8)))
                }
                editingIndex = nil
            }
            Button("Annuler", role: .cancel) { editingIndex = nil }
        }
    }
}
```

- [ ] **Step 2: Add to ConfigView**

After "Nom BMU", add:
```swift
NavigationLink("Noms batteries") {
    BatteryLabelsView()
}
```

- [ ] **Step 3: Add to Xcode project pbxproj + build + commit**

---

### Task 8: iOS — Victron Devices in SystemView

**Files:**
- Modify: `iosApp/KXKMBmu/Views/System/SystemView.swift`

- [ ] **Step 1: Add Victron section**

After WiFi/BLE info, add:

```swift
if !bleManager.victronDevices.isEmpty {
    Section("Victron") {
        ForEach(bleManager.victronDevices) { dev in
            HStack {
                Image(systemName: dev.recordType == 0x01 ? "sun.max" :
                      dev.recordType == 0x02 ? "battery.75percent" : "bolt")
                    .foregroundColor(dev.decrypted ? .green : .gray)
                if dev.decrypted {
                    VStack(alignment: .leading) {
                        if dev.recordType == 0x01 {
                            let ppv = Int(dev.rawData.readUInt16LE(at: 4))
                            let vbat = Float(dev.rawData.readUInt16LE(at: 8)) / 100.0
                            Text(String(format: "%.1fV  %dW", vbat, ppv))
                        } else if dev.recordType == 0x02 {
                            let v = Float(dev.rawData.readUInt16LE(at: 2)) / 100.0
                            let soc = Int(dev.rawData.readUInt16LE(at: 6)) / 10
                            Text(String(format: "%.1fV  %d%%", v, soc))
                        }
                    }
                } else {
                    Text(dev.id)
                        .foregroundColor(.secondary)
                    Text("(locked)")
                        .font(.caption)
                        .foregroundColor(.orange)
                }
            }
        }
    }
}
```

- [ ] **Step 2: Build + commit**

---

## Phase 3: Cleanup

### Task 9: Fix comments + logs + docs

**Files:**
- Modify: `firmware-idf/components/bmu_ble/bmu_ble_battery_svc.cpp` (comment fix)
- Modify: `iosApp/KXKMBmu/Stubs/BleManager.swift` (Victron log suppress)
- Modify: `iosApp/CLAUDE.md` (GATT table update)

- [ ] **Step 1: Fix firmware comment**

In `bmu_ble_battery_svc.cpp` line 30, change "15 octets" to "18 bytes (packed)".

- [ ] **Step 2: Suppress Victron unknown service log**

In BleManager `didUpdateValueFor`, replace the "Unknown char 68C100xx" log:

```swift
    // Victron SmartShunt service — for VictronConnect, not iOS
    let kVictronSvcUUID = CBUUID(string: "68C10001-B17F-4D3A-A290-34AD6499937C")
    if parentSvcUUID == kVictronSvcUUID { return }

    logBle("Unknown char \(uuid.uuidString.prefix(12)) from svc \(parentSvcUUID?.uuidString.prefix(12) ?? "nil")")
```

- [ ] **Step 3: Update iosApp/CLAUDE.md GATT table**

Add chars 0x0036 (labels R/W), 0x003B (balancer R/N), 0x0026 (Victron scan R) to the GATT table.

- [ ] **Step 4: Build both + commit + push**

```bash
cd firmware-idf && idf.py build
cd ../iosApp && xcodebuild build -scheme KXKMBmu ...
git add -A && git commit -m "fix: cleanup comments + logs + docs"
git push origin main
```

---

## Task Dependency Graph

```
Task 1 (balancer char) ────────┐
Task 2 (Victron scan char) ────┤
Task 3 (label READ) ───────────┼── Task 4 (iOS parsing) ── Task 5 (detail views)
                                │                          Task 6 (cell badges)
                                │                          Task 7 (labels config)
                                │                          Task 8 (Victron system)
                                └── Task 9 (cleanup)
```

Tasks 1-3 are independent firmware work. Task 4 parses all new chars. Tasks 5-8 are independent iOS UI. Task 9 is independent cleanup.
