# Phase 1: BLE SOH + App Display — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose SOH and R_int data via BLE and display it in the KMP mobile app with on-demand measurement trigger.

**Architecture:** New BLE characteristic 0x003A for SOH data. App extends BatteryState model, adds GattParser for 0x0039/0x003A, and displays health data with R_int trigger button.

**Tech Stack:** ESP-IDF 5.4, NimBLE, Kotlin Multiplatform (KMP), Compose/SwiftUI

---

## File Structure

| Action | Path | Responsibility |
|--------|------|----------------|
| Modify | `firmware-idf/components/bmu_ble/Kconfig` | Add `BMU_BLE_SOH_ENABLED` toggle |
| Modify | `firmware-idf/components/bmu_ble/bmu_ble_battery_svc.cpp` | SOH characteristic 0x003A + notify |
| Modify | `firmware-idf/components/bmu_ble/CMakeLists.txt` | Add `bmu_soh` dependency |
| Create | `firmware-idf/test/test_ble_soh/CMakeLists.txt` | Test build |
| Create | `firmware-idf/test/test_ble_soh/main/CMakeLists.txt` | Test main build |
| Create | `firmware-idf/test/test_ble_soh/main/test_ble_soh.cpp` | Host unit tests: SOH BLE pack/unpack |
| Modify | `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/model/BatteryState.kt` | Add `BatteryHealth` data class |
| Modify | `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/transport/GattParser.kt` | Parse 0x003A (SOH) and 0x0039 (R_int) |
| Modify | `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/transport/BleTransport.kt` | Subscribe to SOH notify, R_int trigger/poll |
| Modify | `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/transport/Transport.kt` | Add `observeHealth()` + `triggerRint()` |
| Modify | `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/domain/MonitoringUseCase.kt` | Expose health flow |
| Modify | `kxkm-bmu-app/shared/src/commonTest/kotlin/com/kxkm/bmu/GattParserTest.kt` | Tests for SOH + R_int parsing |
| Modify | `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/detail/BatteryDetailScreen.kt` | SOH gauge + R_int card + trigger |
| Modify | `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/viewmodel/BatteryDetailViewModel.kt` | Health state + trigger action |
| Modify | `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/dashboard/BatteryCellCard.kt` | SOH badge on card |

---

### Task 1: Firmware — Kconfig + BLE SOH characteristic (0x003A)

**Files:**
- Modify: `firmware-idf/components/bmu_ble/Kconfig`
- Modify: `firmware-idf/components/bmu_ble/CMakeLists.txt`
- Modify: `firmware-idf/components/bmu_ble/bmu_ble_battery_svc.cpp`

- [ ] **Step 1: Add Kconfig toggle**

Add to `firmware-idf/components/bmu_ble/Kconfig`, inside the existing `menu "BMU Bluetooth"` block, before `endmenu`:

```
    config BMU_BLE_SOH_ENABLED
        bool "Enable BLE SOH characteristic"
        default y
        depends on BMU_BLE_ENABLED
        depends on BMU_RINT_ENABLED
        help
            Expose SOH and R_int summary via BLE characteristic 0x003A.
            Requires bmu_soh and bmu_rint components.
```

- [ ] **Step 2: Add bmu_soh dependency to CMakeLists**

Modify `firmware-idf/components/bmu_ble/CMakeLists.txt` — add `bmu_soh` to the REQUIRES list:

```cmake
idf_component_register(
    SRCS "bmu_ble.cpp" "bmu_ble_battery_svc.cpp" "bmu_ble_system_svc.cpp" "bmu_ble_control_svc.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bt bmu_protection bmu_config nvs_flash esp_timer bmu_rint bmu_soh
    PRIV_REQUIRES bmu_vedirect bmu_wifi
)
```

- [ ] **Step 3: Add SOH characteristic to bmu_ble_battery_svc.cpp**

In `bmu_ble_battery_svc.cpp`, add the SOH packed struct, UUID, callback, and integrate into the GATT service definition and notify timer.

After the existing `#if CONFIG_BMU_RINT_ENABLED` block (line 175), add the SOH section guarded by `CONFIG_BMU_BLE_SOH_ENABLED`:

```cpp
/* ── SOH BLE struct et UUID (optionnel, CONFIG_BMU_BLE_SOH_ENABLED) ──── */
#if CONFIG_BMU_BLE_SOH_ENABLED

#include "bmu_soh.h"

typedef struct __attribute__((packed)) {
    uint8_t  soh_pct;           /* SOH 0-100 (bmu_soh_get_cached * 100) */
    uint16_t r_ohmic_mohm_x10;  /* R_ohmic * 10 (0.1 mOhm resolution) */
    uint16_t r_total_mohm_x10;  /* R_total * 10 */
    uint8_t  rint_valid;        /* R_int measurement valid flag */
    uint8_t  soh_confidence;    /* Model confidence 0-100 (sample count proxy) */
} ble_soh_char_t;               /* 7 bytes per battery */

static ble_uuid128_t s_soh_result_uuid = BMU_BLE_UUID128_DECLARE(0x3A, 0x00);
static uint16_t s_soh_val_handle;

static void build_soh_payload(int idx, ble_soh_char_t *out)
{
    float soh = bmu_soh_get_cached(idx);
    out->soh_pct = (soh >= 0.0f) ? (uint8_t)(soh * 100.0f) : 0;

    bmu_rint_result_t rint = bmu_rint_get_cached((uint8_t)idx);
    out->r_ohmic_mohm_x10 = (uint16_t)(rint.r_ohmic_mohm * 10.0f);
    out->r_total_mohm_x10 = (uint16_t)(rint.r_total_mohm * 10.0f);
    out->rint_valid        = (uint8_t)(rint.valid ? 1 : 0);

    /* Confidence proxy: clamp SOH accumulator sample count to 0-100 */
    /* soh_pct == 0 && soh < 0 means "not yet computed" → confidence 0 */
    out->soh_confidence = (soh >= 0.0f) ? 100 : 0;
}

/* Callback READ — retourne SOH pour toutes les batteries en sequence */
static int soh_result_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t nb_ina = bmu_ble_get_nb_ina();

    for (int i = 0; i < nb_ina; i++) {
        ble_soh_char_t payload;
        build_soh_payload(i, &payload);
        int rc = os_mbuf_append(ctxt->om, &payload, sizeof(payload));
        if (rc != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    }

    return 0;
}

#endif /* CONFIG_BMU_BLE_SOH_ENABLED */
```

- [ ] **Step 4: Expand GATT characteristic array and registration**

Change the array size to accommodate the SOH characteristic. The current array is:
```cpp
static struct ble_gatt_chr_def s_bat_chr_defs[BMU_MAX_BATTERIES + 3];
```

Change to:
```cpp
static struct ble_gatt_chr_def s_bat_chr_defs[BMU_MAX_BATTERIES + 4];
```

In `bmu_ble_battery_svc_defs()`, after the R_int characteristics registration block, before the `#else` / terminator, add the SOH characteristic:

```cpp
#if CONFIG_BMU_BLE_SOH_ENABLED
        int soh_base = rint_base + 2;
        s_bat_chr_defs[soh_base].uuid       = &s_soh_result_uuid.u;
        s_bat_chr_defs[soh_base].access_cb  = soh_result_access_cb;
        s_bat_chr_defs[soh_base].arg        = NULL;
        s_bat_chr_defs[soh_base].flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
        s_bat_chr_defs[soh_base].val_handle = &s_soh_val_handle;

        /* Terminateur */
        memset(&s_bat_chr_defs[soh_base + 1], 0, sizeof(struct ble_gatt_chr_def));
#else
        /* Terminateur (RINT sans SOH) */
        memset(&s_bat_chr_defs[BMU_MAX_BATTERIES + 2], 0, sizeof(struct ble_gatt_chr_def));
#endif
```

Remove the existing `memset` terminator that was at `BMU_MAX_BATTERIES + 2` inside the `#if CONFIG_BMU_RINT_ENABLED` block, since it is now handled by the SOH or RINT-only terminator above.

- [ ] **Step 5: Add SOH to the notify timer callback**

In `notify_timer_cb()`, after the existing battery notification loop, add:

```cpp
#if CONFIG_BMU_BLE_SOH_ENABLED
    /* Notify SOH characteristic (all batteries concatenated) */
    if (s_soh_val_handle != 0) {
        struct os_mbuf *om_soh = ble_hs_mbuf_from_flat(NULL, 0);
        if (om_soh) {
            bool ok = true;
            for (int i = 0; i < nb_ina && ok; i++) {
                ble_soh_char_t soh_payload;
                build_soh_payload(i, &soh_payload);
                if (os_mbuf_append(om_soh, &soh_payload, sizeof(soh_payload)) != 0) {
                    ok = false;
                }
            }
            if (ok) {
                int rc = ble_gatts_notify_custom(0xFFFF, s_soh_val_handle, om_soh);
                if (rc != 0 && rc != BLE_HS_ENOTCONN) {
                    ESP_LOGD(TAG, "Notify SOH rc=%d", rc);
                }
            } else {
                os_mbuf_free_chain(om_soh);
            }
        }
    }
#endif
```

**Commit message:** `feat(ble): add SOH characteristic 0x003A with READ+NOTIFY`

**Verification:**
```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/firmware-idf
source ~/esp/esp-idf/export.sh && idf.py build
```

---

### Task 2: Firmware host test — SOH BLE pack/unpack

**Files:**
- Create: `firmware-idf/test/test_ble_soh/CMakeLists.txt`
- Create: `firmware-idf/test/test_ble_soh/main/CMakeLists.txt`
- Create: `firmware-idf/test/test_ble_soh/main/test_ble_soh.cpp`

- [ ] **Step 1: Create test build files**

Create `firmware-idf/test/test_ble_soh/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_ble_soh)
```

Create `firmware-idf/test/test_ble_soh/main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "test_ble_soh.cpp"
    INCLUDE_DIRS "."
    REQUIRES unity
)
```

- [ ] **Step 2: Write pack/unpack tests**

Create `firmware-idf/test/test_ble_soh/main/test_ble_soh.cpp`:
```cpp
/**
 * @file test_ble_soh.cpp
 * @brief Tests unitaires BLE SOH characteristic pack/unpack — host (Unity)
 *
 * Verifie le format binaire de la struct ble_soh_char_t (7 octets, little-endian).
 * Pas de hardware necessaire.
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cmath>

/* ── Struct miroir de bmu_ble_battery_svc.cpp ────────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  soh_pct;
    uint16_t r_ohmic_mohm_x10;
    uint16_t r_total_mohm_x10;
    uint8_t  rint_valid;
    uint8_t  soh_confidence;
} ble_soh_char_t;

static_assert(sizeof(ble_soh_char_t) == 7, "ble_soh_char_t must be 7 bytes");

/* ── Helper: pack payload manually ───────────────────────────────────── */

static void build_test_payload(ble_soh_char_t *out,
                                float soh_0_1,
                                float r_ohmic_mohm, float r_total_mohm,
                                bool valid, uint8_t confidence)
{
    out->soh_pct           = (soh_0_1 >= 0.0f) ? (uint8_t)(soh_0_1 * 100.0f) : 0;
    out->r_ohmic_mohm_x10  = (uint16_t)(r_ohmic_mohm * 10.0f);
    out->r_total_mohm_x10  = (uint16_t)(r_total_mohm * 10.0f);
    out->rint_valid        = valid ? 1 : 0;
    out->soh_confidence    = confidence;
}

/* ── Helper: unpack from raw bytes (simulates app parser) ────────────── */

typedef struct {
    int   soh_pct;
    float r_ohmic_mohm;
    float r_total_mohm;
    bool  rint_valid;
    int   soh_confidence;
} parsed_soh_t;

static parsed_soh_t unpack_soh(const uint8_t *buf)
{
    parsed_soh_t p;
    p.soh_pct       = buf[0];
    p.r_ohmic_mohm  = (float)((uint16_t)(buf[1] | (buf[2] << 8))) / 10.0f;
    p.r_total_mohm  = (float)((uint16_t)(buf[3] | (buf[4] << 8))) / 10.0f;
    p.rint_valid    = buf[5] != 0;
    p.soh_confidence = buf[6];
    return p;
}

/* ── Tests ───────────────────────────────────────────────────────────── */

void test_soh_pack_healthy_battery(void)
{
    ble_soh_char_t payload;
    build_test_payload(&payload, 0.92f, 15.2f, 18.5f, true, 100);

    const uint8_t *raw = (const uint8_t *)&payload;
    parsed_soh_t p = unpack_soh(raw);

    TEST_ASSERT_EQUAL_INT(92, p.soh_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 15.2f, p.r_ohmic_mohm);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 18.5f, p.r_total_mohm);
    TEST_ASSERT_TRUE(p.rint_valid);
    TEST_ASSERT_EQUAL_INT(100, p.soh_confidence);
}

void test_soh_pack_invalid_rint(void)
{
    ble_soh_char_t payload;
    build_test_payload(&payload, 0.85f, 0.0f, 0.0f, false, 50);

    const uint8_t *raw = (const uint8_t *)&payload;
    parsed_soh_t p = unpack_soh(raw);

    TEST_ASSERT_EQUAL_INT(85, p.soh_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, p.r_ohmic_mohm);
    TEST_ASSERT_FALSE(p.rint_valid);
    TEST_ASSERT_EQUAL_INT(50, p.soh_confidence);
}

void test_soh_pack_not_yet_computed(void)
{
    ble_soh_char_t payload;
    build_test_payload(&payload, -1.0f, 0.0f, 0.0f, false, 0);

    const uint8_t *raw = (const uint8_t *)&payload;
    parsed_soh_t p = unpack_soh(raw);

    TEST_ASSERT_EQUAL_INT(0, p.soh_pct);
    TEST_ASSERT_FALSE(p.rint_valid);
    TEST_ASSERT_EQUAL_INT(0, p.soh_confidence);
}

void test_soh_pack_boundary_100_percent(void)
{
    ble_soh_char_t payload;
    build_test_payload(&payload, 1.0f, 5.0f, 6.0f, true, 100);

    const uint8_t *raw = (const uint8_t *)&payload;
    parsed_soh_t p = unpack_soh(raw);

    TEST_ASSERT_EQUAL_INT(100, p.soh_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.0f, p.r_ohmic_mohm);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 6.0f, p.r_total_mohm);
}

void test_soh_pack_high_resistance(void)
{
    /* R_ohmic = 200 mOhm → x10 = 2000 (fits in uint16) */
    ble_soh_char_t payload;
    build_test_payload(&payload, 0.45f, 200.0f, 350.0f, true, 80);

    const uint8_t *raw = (const uint8_t *)&payload;
    parsed_soh_t p = unpack_soh(raw);

    TEST_ASSERT_EQUAL_INT(45, p.soh_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 200.0f, p.r_ohmic_mohm);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 350.0f, p.r_total_mohm);
    TEST_ASSERT_TRUE(p.rint_valid);
}

void test_soh_struct_size(void)
{
    TEST_ASSERT_EQUAL_INT(7, sizeof(ble_soh_char_t));
}

void test_soh_multi_battery_concat(void)
{
    /* Simulate 4 batteries concatenated (as sent in BLE characteristic) */
    uint8_t buf[7 * 4];
    for (int i = 0; i < 4; i++) {
        ble_soh_char_t payload;
        float soh = 0.90f - (i * 0.05f);
        float r_ohm = 10.0f + i * 5.0f;
        float r_tot = 15.0f + i * 7.0f;
        build_test_payload(&payload, soh, r_ohm, r_tot, true, 100 - i * 10);
        memcpy(&buf[i * 7], &payload, 7);
    }

    /* Parse each battery from the concatenated buffer */
    for (int i = 0; i < 4; i++) {
        parsed_soh_t p = unpack_soh(&buf[i * 7]);
        int expected_soh = (int)((0.90f - i * 0.05f) * 100.0f);
        TEST_ASSERT_INT_WITHIN(1, expected_soh, p.soh_pct);
        TEST_ASSERT_TRUE(p.rint_valid);
        TEST_ASSERT_EQUAL_INT(100 - i * 10, p.soh_confidence);
    }
}

extern "C" void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_soh_struct_size);
    RUN_TEST(test_soh_pack_healthy_battery);
    RUN_TEST(test_soh_pack_invalid_rint);
    RUN_TEST(test_soh_pack_not_yet_computed);
    RUN_TEST(test_soh_pack_boundary_100_percent);
    RUN_TEST(test_soh_pack_high_resistance);
    RUN_TEST(test_soh_multi_battery_concat);
    UNITY_END();
}
```

**Commit message:** `test(ble): host tests for SOH characteristic pack/unpack`

**Verification:**
```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/firmware-idf
source ~/esp/esp-idf/export.sh
cd test/test_ble_soh && idf.py build && idf.py -p /dev/null flash monitor 2>&1 | head -50
# Or if using qemu/host runner:
idf.py --preview set-target linux && idf.py build && ./build/test_ble_soh.elf
```

---

### Task 3: App — BatteryHealth data model

**Files:**
- Modify: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/model/BatteryState.kt`

- [ ] **Step 1: Add BatteryHealth data class**

Add to `BatteryState.kt` after the existing `BatteryState` data class:

```kotlin
@Serializable
data class BatteryHealth(
    val index: Int,
    val sohPercent: Int,           // 0-100, 0 = not computed
    val rOhmicMohm: Float,        // Ohmic resistance in mOhm
    val rTotalMohm: Float,        // Total resistance in mOhm
    val rintValid: Boolean,       // R_int measurement valid
    val sohConfidence: Int,       // 0-100
    val timestamp: Long = 0L      // Epoch ms (set by app on reception)
)
```

**Commit message:** `feat(app): add BatteryHealth data model`

---

### Task 4: App — GattParser extension for 0x003A (SOH) and 0x0039 (R_int result)

**Files:**
- Modify: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/transport/GattParser.kt`
- Modify: `kxkm-bmu-app/shared/src/commonTest/kotlin/com/kxkm/bmu/GattParserTest.kt`

- [ ] **Step 1: Write failing tests first**

Add to `GattParserTest.kt`:

```kotlin
@Test
fun parseSohCharacteristic() {
    // 7 bytes: soh_pct(u8) + r_ohmic_x10(u16 LE) + r_total_x10(u16 LE) + valid(u8) + confidence(u8)
    // soh = 92, r_ohmic = 15.2 mOhm (x10 = 152 = 0x0098 LE = 98 00)
    // r_total = 18.5 mOhm (x10 = 185 = 0x00B9 LE = B9 00), valid = 1, confidence = 100
    val bytes = byteArrayOf(
        92.toByte(),                   // soh_pct
        0x98.toByte(), 0x00,           // r_ohmic_mohm_x10 = 152
        0xB9.toByte(), 0x00,           // r_total_mohm_x10 = 185
        0x01,                          // rint_valid
        100.toByte()                   // soh_confidence
    )

    val health = GattParser.parseSohSingle(0, bytes, 0)
    assertEquals(92, health.sohPercent)
    assertEquals(15.2f, health.rOhmicMohm, 0.15f)
    assertEquals(18.5f, health.rTotalMohm, 0.15f)
    assertEquals(true, health.rintValid)
    assertEquals(100, health.sohConfidence)
}

@Test
fun parseSohMultipleBatteries() {
    // 2 batteries x 7 bytes = 14 bytes
    val bytes = byteArrayOf(
        // Battery 0: SOH 92, r_ohm 15.2, r_tot 18.5, valid, confidence 100
        92.toByte(), 0x98.toByte(), 0x00, 0xB9.toByte(), 0x00, 0x01, 100.toByte(),
        // Battery 1: SOH 85, r_ohm 22.0, r_tot 30.0, valid, confidence 80
        85.toByte(), 0xDC.toByte(), 0x00, 0x2C, 0x01, 0x01, 80.toByte()
    )

    val list = GattParser.parseSohAll(bytes)
    assertEquals(2, list.size)
    assertEquals(92, list[0].sohPercent)
    assertEquals(85, list[1].sohPercent)
    assertEquals(22.0f, list[1].rOhmicMohm, 0.15f)
    assertEquals(30.0f, list[1].rTotalMohm, 0.15f)
    assertEquals(80, list[1].sohConfidence)
}

@Test
fun parseRintResult() {
    // 11 bytes per battery: r_ohmic_x10(u16) + r_total_x10(u16) + v_load(u16) + v_ocv(u16) + i_load(i16) + valid(u8)
    // r_ohmic = 15.2 (x10=152=0x98,0x00), r_total = 18.5 (x10=185=0xB9,0x00)
    // v_load = 26500 (0x74,0x67), v_ocv = 27000 (0x78,0x69)
    // i_load = 5200 mA (0x50,0x14), valid = 1
    val bytes = byteArrayOf(
        0x98.toByte(), 0x00,           // r_ohmic_mohm_x10
        0xB9.toByte(), 0x00,           // r_total_mohm_x10
        0x74, 0x67,                    // v_load_mv = 26484
        0x78, 0x69,                    // v_ocv_mv = 26999 (0x6978 LE)
        0x50, 0x14,                    // i_load_ma = 5200
        0x01                           // valid
    )

    val result = GattParser.parseRintSingle(0, bytes, 0)
    assertEquals(15.2f, result.rOhmicMohm, 0.15f)
    assertEquals(18.5f, result.rTotalMohm, 0.15f)
    assertEquals(true, result.rintValid)
}
```

- [ ] **Step 2: Implement GattParser methods**

Add to `GattParser.kt`, before the `// -- Little-endian helpers --` section:

```kotlin
/** Parse single SOH characteristic (7 bytes) from buffer at offset */
fun parseSohSingle(index: Int, bytes: ByteArray, offset: Int): BatteryHealth {
    val o = offset
    return BatteryHealth(
        index = index,
        sohPercent = bytes[o].toInt() and 0xFF,
        rOhmicMohm = readUInt16LE(bytes, o + 1) / 10.0f,
        rTotalMohm = readUInt16LE(bytes, o + 3) / 10.0f,
        rintValid = bytes[o + 5].toInt() != 0,
        sohConfidence = bytes[o + 6].toInt() and 0xFF
    )
}

/** Parse concatenated SOH characteristics (7 bytes per battery) */
fun parseSohAll(bytes: ByteArray): List<BatteryHealth> {
    val perBattery = 7
    val count = bytes.size / perBattery
    return (0 until count).map { i ->
        parseSohSingle(i, bytes, i * perBattery)
    }
}

/** Data class for R_int BLE result (per battery, 11 bytes) */
data class RintResult(
    val index: Int,
    val rOhmicMohm: Float,
    val rTotalMohm: Float,
    val vLoadMv: Int,
    val vOcvMv: Int,
    val iLoadMa: Int,
    val rintValid: Boolean
)

/** Parse single R_int result (11 bytes) from buffer at offset */
fun parseRintSingle(index: Int, bytes: ByteArray, offset: Int): RintResult {
    val o = offset
    return RintResult(
        index = index,
        rOhmicMohm = readUInt16LE(bytes, o) / 10.0f,
        rTotalMohm = readUInt16LE(bytes, o + 2) / 10.0f,
        vLoadMv = readUInt16LE(bytes, o + 4),
        vOcvMv = readUInt16LE(bytes, o + 6),
        iLoadMa = readInt16LE(bytes, o + 8),
        rintValid = bytes[o + 10].toInt() != 0
    )
}

/** Parse concatenated R_int results (11 bytes per battery) */
fun parseRintAll(bytes: ByteArray): List<RintResult> {
    val perBattery = 11
    val count = bytes.size / perBattery
    return (0 until count).map { i ->
        parseRintSingle(i, bytes, i * perBattery)
    }
}

/** Encode R_int trigger command: single byte = battery index (0xFF = all) */
fun encodeRintTrigger(batteryIndex: Int): ByteArray {
    return byteArrayOf(
        if (batteryIndex < 0) 0xFF.toByte() else batteryIndex.toByte()
    )
}
```

**Commit message:** `feat(app): GattParser extensions for SOH 0x003A and R_int 0x0039`

**Verification:**
```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/kxkm-bmu-app
./gradlew :shared:jvmTest
```

---

### Task 5: App — Transport interface + BleTransport for SOH observe and R_int trigger

**Files:**
- Modify: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/transport/Transport.kt`
- Modify: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/transport/BleTransport.kt`
- Modify: `kxkm-bmu-app/shared/src/commonMain/kotlin/com/kxkm/bmu/domain/MonitoringUseCase.kt`

- [ ] **Step 1: Extend Transport interface**

Add to `Transport.kt`, inside the `Transport` interface:

```kotlin
/** Reactive battery health stream (SOH + R_int summary) */
fun observeHealth(): Flow<List<BatteryHealth>> = emptyFlow()

/** Trigger R_int measurement on a specific battery (or all with index=-1) */
suspend fun triggerRintMeasurement(batteryIndex: Int): CommandResult =
    CommandResult.error("Not supported")
```

Add the import at the top:
```kotlin
import kotlinx.coroutines.flow.emptyFlow
```

- [ ] **Step 2: Implement in BleTransport**

Add a new StateFlow for health data and subscribe to SOH notifications:

```kotlin
// Add field to BleTransport class:
private val _health = MutableStateFlow<List<BatteryHealth>>(emptyList())

override fun observeHealth(): Flow<List<BatteryHealth>> = _health
```

In `subscribeToNotifications()`, add after the solar subscription block:

```kotlin
// Subscribe to SOH notifications (0x003A)
scope.launch {
    try {
        p.observe(characteristicOf(BATTERY_SVC, chrUuid(0x003A))).collect { bytes ->
            val healthList = GattParser.parseSohAll(bytes)
            _health.value = healthList
        }
    } catch (_: Exception) { /* SOH characteristic may not be present */ }
}
```

Add `triggerRintMeasurement`:

```kotlin
override suspend fun triggerRintMeasurement(batteryIndex: Int): CommandResult {
    val p = peripheral ?: return CommandResult.error("Not connected")
    p.write(
        characteristicOf(BATTERY_SVC, chrUuid(0x0038)),
        GattParser.encodeRintTrigger(batteryIndex)
    )
    // Poll R_int result after measurement (~1.2s)
    delay(1500)
    return try {
        val resultBytes = p.read(characteristicOf(BATTERY_SVC, chrUuid(0x0039)))
        val results = GattParser.parseRintAll(resultBytes)
        val target = if (batteryIndex >= 0) results.getOrNull(batteryIndex) else null
        if (target?.rintValid == true || batteryIndex < 0) CommandResult.ok()
        else CommandResult.error("Measurement invalid")
    } catch (e: Exception) {
        CommandResult.error(e.message ?: "Read failed")
    }
}
```

- [ ] **Step 3: Expose health in MonitoringUseCase**

Add to `MonitoringUseCase.kt`:

```kotlin
fun observeHealth(): Flow<List<BatteryHealth>> = transport.observeHealth()

fun observeHealth(index: Int): Flow<BatteryHealth?> =
    transport.observeHealth().map { it.firstOrNull { h -> h.index == index } }

suspend fun triggerRintMeasurement(batteryIndex: Int): CommandResult =
    transport.triggerRintMeasurement(batteryIndex)

/** Callback-based API */
fun observeHealth(callback: (List<BatteryHealth>) -> Unit) {
    scope.launch {
        observeHealth().collect { callback(it) }
    }
}

fun observeHealth(index: Int, callback: (BatteryHealth?) -> Unit) {
    scope.launch {
        observeHealth(index).collect { callback(it) }
    }
}
```

**Commit message:** `feat(app): BLE SOH observe + R_int trigger in transport layer`

**Verification:**
```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/kxkm-bmu-app
./gradlew :shared:jvmTest
```

---

### Task 6: App — Dashboard UI (SOH badge on BatteryCellCard + detail screen health card)

**Files:**
- Modify: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/dashboard/BatteryCellCard.kt`
- Modify: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/viewmodel/BatteryDetailViewModel.kt`
- Modify: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/ui/detail/BatteryDetailScreen.kt`
- Modify: `kxkm-bmu-app/androidApp/src/main/java/com/kxkm/bmu/viewmodel/DashboardViewModel.kt`

- [ ] **Step 1: Add SOH badge to BatteryCellCard**

Modify `BatteryCellCard.kt` to accept an optional `BatteryHealth?` parameter and display an SOH badge:

```kotlin
@Composable
fun BatteryCellCard(
    battery: BatteryState,
    health: BatteryHealth? = null,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
```

Add after the current/voltage display, before the switch count row:

```kotlin
// SOH badge (if available)
health?.let { h ->
    if (h.sohPercent > 0) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            Text(
                text = "SOH",
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Text(
                text = "${h.sohPercent}%",
                style = MaterialTheme.typography.labelMedium,
                fontFamily = FontFamily.Monospace,
                color = sohColor(h.sohPercent),
            )
        }
    }
}
```

Add a color helper:

```kotlin
@Composable
private fun sohColor(percent: Int) = when {
    percent >= 80 -> MaterialTheme.colorScheme.primary     // green/healthy
    percent >= 60 -> MaterialTheme.colorScheme.tertiary    // orange/warning
    else -> MaterialTheme.colorScheme.error                // red/critical
}
```

Import `BatteryHealth`:
```kotlin
import com.kxkm.bmu.shared.model.BatteryHealth
```

- [ ] **Step 2: Add health data to DashboardViewModel**

Modify `DashboardViewModel.kt`:

```kotlin
private val _healthMap = MutableStateFlow<Map<Int, BatteryHealth>>(emptyMap())
val healthMap: StateFlow<Map<Int, BatteryHealth>> = _healthMap.asStateFlow()

// In init block, add:
viewModelScope.launch {
    monitoringUseCase.observeHealth { healthList ->
        _healthMap.value = healthList.associateBy { it.index }
    }
}
```

- [ ] **Step 3: Wire health data in DashboardScreen**

In the screen composable where `BatteryCellCard` is called, pass the health from the map:

```kotlin
val healthMap by viewModel.healthMap.collectAsState()

// In the battery grid:
BatteryCellCard(
    battery = battery,
    health = healthMap[battery.index],
    onClick = { /* navigate to detail */ },
)
```

- [ ] **Step 4: Add SOH + R_int section to BatteryDetailScreen**

Modify `BatteryDetailViewModel.kt` to add:

```kotlin
private val _health = MutableStateFlow<BatteryHealth?>(null)
val health: StateFlow<BatteryHealth?> = _health.asStateFlow()

private val _rintMeasuring = MutableStateFlow(false)
val rintMeasuring: StateFlow<Boolean> = _rintMeasuring.asStateFlow()

// In init block, add:
viewModelScope.launch {
    monitoringUseCase.observeHealth(batteryIndex) { h ->
        _health.value = h
    }
}

fun triggerRintMeasurement() {
    viewModelScope.launch {
        _rintMeasuring.value = true
        val result = monitoringUseCase.triggerRintMeasurement(batteryIndex)
        _commandResult.value = if (result.success) "R_int mesure lancee"
            else "Erreur: ${result.errorMessage}"
        _rintMeasuring.value = false
    }
}
```

- [ ] **Step 5: Add HealthCard composable to BatteryDetailScreen**

Add to `BatteryDetailScreen.kt`, between the StateCard and the chart:

```kotlin
// Health card (SOH + R_int)
val health by viewModel.health.collectAsState()
val rintMeasuring by viewModel.rintMeasuring.collectAsState()

health?.let { h -> HealthCard(h, rintMeasuring, onTriggerRint = { viewModel.triggerRintMeasurement() }) }
```

Add the composable function:

```kotlin
@Composable
private fun HealthCard(
    health: BatteryHealth,
    measuring: Boolean,
    onTriggerRint: () -> Unit,
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = "Sante batterie",
                style = MaterialTheme.typography.titleMedium,
            )

            // SOH gauge row
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column {
                    Text(
                        text = "SOH",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Text(
                        text = if (health.sohPercent > 0) "${health.sohPercent}%" else "--",
                        style = MaterialTheme.typography.headlineMedium,
                        fontFamily = FontFamily.Monospace,
                    )
                }
                Column {
                    Text(
                        text = "Confiance",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Text(
                        text = "${health.sohConfidence}%",
                        style = MaterialTheme.typography.titleMedium,
                        fontFamily = FontFamily.Monospace,
                    )
                }
            }

            // R_int values
            if (health.rintValid) {
                CounterRow("R ohmic", "%.1f m\u03A9".format(health.rOhmicMohm))
                CounterRow("R total", "%.1f m\u03A9".format(health.rTotalMohm))
            } else {
                Text(
                    text = "R_int non disponible",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }

            // Trigger R_int button
            OutlinedButton(
                onClick = onTriggerRint,
                modifier = Modifier.fillMaxWidth(),
                enabled = !measuring,
            ) {
                if (measuring) {
                    Text("Mesure en cours...")
                } else {
                    Icon(Icons.Filled.Speed, contentDescription = null)
                    Text("Mesurer R_int", modifier = Modifier.padding(start = 4.dp))
                }
            }
        }
    }
}
```

Add the missing import:
```kotlin
import androidx.compose.material.icons.filled.Speed
import com.kxkm.bmu.shared.model.BatteryHealth
```

**Commit message:** `feat(app): SOH gauge + R_int display + trigger in dashboard and detail`

**Verification:**
```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/kxkm-bmu-app
./gradlew :androidApp:assembleDebug
```

---

### Task 7: Final integration tests

**Files:**
- Existing: `kxkm-bmu-app/shared/src/commonTest/kotlin/com/kxkm/bmu/GattParserTest.kt`

This task verifies that all tests added in previous tasks pass end-to-end.

- [ ] **Step 1: Run all KMP tests**

```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/kxkm-bmu-app
./gradlew :shared:allTests
```

- [ ] **Step 2: Run firmware host test**

```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/firmware-idf
source ~/esp/esp-idf/export.sh && idf.py build
```

- [ ] **Step 3: Run full Android build**

```bash
cd /Users/electron/Documents/Lelectron_rare/1-KXKM/KXKM_Batterie_Parallelator/kxkm-bmu-app
./gradlew assembleDebug
```

**Commit message:** `test: verify Phase 1 BLE SOH + app integration`

---

## UUID Summary

| UUID | Direction | Format | Description |
|------|-----------|--------|-------------|
| 0x0038 | WRITE | 1 byte (battery index, 0xFF = all) | R_int measurement trigger |
| 0x0039 | READ | 11 bytes x N batteries | R_int result (existing) |
| 0x003A | READ+NOTIFY | 7 bytes x N batteries | SOH + R_int summary (new) |

## BLE Payload Formats

### 0x003A — SOH Characteristic (7 bytes per battery, little-endian)

| Offset | Type | Field | Unit |
|--------|------|-------|------|
| 0 | uint8 | soh_pct | 0-100% |
| 1 | uint16 | r_ohmic_mohm_x10 | 0.1 mOhm |
| 3 | uint16 | r_total_mohm_x10 | 0.1 mOhm |
| 5 | uint8 | rint_valid | 0/1 |
| 6 | uint8 | soh_confidence | 0-100 |

### 0x0039 — R_int Result (11 bytes per battery, little-endian)

| Offset | Type | Field | Unit |
|--------|------|-------|------|
| 0 | uint16 | r_ohmic_mohm_x10 | 0.1 mOhm |
| 2 | uint16 | r_total_mohm_x10 | 0.1 mOhm |
| 4 | uint16 | v_load_mv | mV |
| 6 | uint16 | v_ocv_mv | mV |
| 8 | int16 | i_load_ma | mA |
| 10 | uint8 | valid | 0/1 |
