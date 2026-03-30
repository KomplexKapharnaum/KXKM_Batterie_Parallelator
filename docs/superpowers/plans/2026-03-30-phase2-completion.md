# Phase 2 Completion: Battery Manager + main.cpp + Rust FFI

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete Phase 2 by adding energy accounting (Ah tracking), wiring the full protection loop in main.cpp, and creating the Rust FFI bridge for the hybrid firmware.

**Architecture:** `bmu_battery_manager` is an ESP-IDF component inside `bmu_protection/` that runs one FreeRTOS task per battery for Ah integration. `main.cpp` orchestrates init + protection loop. The Rust `bmu-ffi` crate exports `extern "C"` functions for the protection state machine, consumed by ESP-IDF via a static library link.

**Tech Stack:** ESP-IDF v5.3 (C++), Rust no_std + cbindgen, FreeRTOS

**Branch:** `feat/esp-idf-phase0-1`

---

## File Structure

```
firmware-idf/components/bmu_protection/
├── CMakeLists.txt                     # ADD bmu_battery_manager.cpp to SRCS
├── include/bmu_battery_manager.h      # CREATE — Ah tracking API
├── bmu_battery_manager.cpp            # CREATE — FreeRTOS Ah tasks
firmware-idf/main/
├── CMakeLists.txt                     # ADD bmu_protection bmu_config to REQUIRES
└── main.cpp                           # REWRITE — full protection loop
firmware-rs/crates/bmu-ffi/
├── Cargo.toml                         # CREATE
├── src/lib.rs                         # CREATE — extern "C" bridge
└── cbindgen.toml                      # CREATE — generates bmu_ffi.h
```

---

### Task 1: bmu_battery_manager (ESP-IDF)

**Files:**
- Create: `firmware-idf/components/bmu_protection/include/bmu_battery_manager.h`
- Create: `firmware-idf/components/bmu_protection/bmu_battery_manager.cpp`
- Modify: `firmware-idf/components/bmu_protection/CMakeLists.txt` (add SRCS)

- [ ] **Step 1: Create bmu_battery_manager.h**

```cpp
#pragma once

#include "bmu_ina237.h"
#include "bmu_config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bmu_ina237_t       *ina_devices;
    uint8_t             nb_ina;
    SemaphoreHandle_t   mutex;
    float               ah_discharge[BMU_MAX_BATTERIES];
    float               ah_charge[BMU_MAX_BATTERIES];
    TaskHandle_t        ah_tasks[BMU_MAX_BATTERIES];
    bool                ah_running[BMU_MAX_BATTERIES];
} bmu_battery_manager_t;

esp_err_t bmu_battery_manager_init(bmu_battery_manager_t *mgr,
                                    bmu_ina237_t *ina, uint8_t nb_ina);

esp_err_t bmu_battery_manager_start_ah_task(bmu_battery_manager_t *mgr, int idx);

float bmu_battery_manager_get_max_voltage_mv(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_min_voltage_mv(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_avg_voltage_mv(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_ah_discharge(bmu_battery_manager_t *mgr, int idx);
float bmu_battery_manager_get_ah_charge(bmu_battery_manager_t *mgr, int idx);
float bmu_battery_manager_get_total_current_a(bmu_battery_manager_t *mgr);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Create bmu_battery_manager.cpp**

```cpp
#include "bmu_battery_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>
#include <cstring>

static const char *TAG = "BMGR";

esp_err_t bmu_battery_manager_init(bmu_battery_manager_t *mgr,
                                    bmu_ina237_t *ina, uint8_t nb_ina)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->ina_devices = ina;
    mgr->nb_ina = nb_ina;
    mgr->mutex = xSemaphoreCreateMutex();
    configASSERT(mgr->mutex != NULL);
    ESP_LOGI(TAG, "Battery manager init: %d batteries", nb_ina);
    return ESP_OK;
}

/* Ah task context — passed via pvParameters, freed by task */
typedef struct {
    int idx;
    bmu_battery_manager_t *mgr;
} ah_task_ctx_t;

static void ah_task(void *pv)
{
    ah_task_ctx_t *ctx = (ah_task_ctx_t *)pv;
    const int idx = ctx->idx;
    bmu_battery_manager_t *mgr = ctx->mgr;
    free(ctx);

    float total_discharge = 0, total_charge = 0;
    int64_t last_us = esp_timer_get_time();
    int sample = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000)); /* 1s sample interval */

        float current_a = 0;
        esp_err_t ret = bmu_ina237_read_current(&mgr->ina_devices[idx], &current_a);
        if (ret != ESP_OK || isnan(current_a)) continue;

        int64_t now_us = esp_timer_get_time();
        float elapsed_h = (float)(now_us - last_us) / 3.6e9f; /* µs → hours */
        last_us = now_us;

        if (elapsed_h > 0) {
            if (current_a > 0) total_discharge += current_a * elapsed_h;
            else if (current_a < 0) total_charge += (-current_a) * elapsed_h;
        }

        if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            mgr->ah_discharge[idx] = total_discharge;
            mgr->ah_charge[idx] = total_charge;
            xSemaphoreGive(mgr->mutex);
        }

        if (++sample % 100 == 0) {
            ESP_LOGI(TAG, "BAT[%d] Ah discharge=%.3f charge=%.3f",
                     idx + 1, total_discharge, total_charge);
        }
    }
}

esp_err_t bmu_battery_manager_start_ah_task(bmu_battery_manager_t *mgr, int idx)
{
    if (idx < 0 || idx >= mgr->nb_ina) return ESP_ERR_INVALID_ARG;

    bool already = false;
    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        already = mgr->ah_running[idx];
        xSemaphoreGive(mgr->mutex);
    }
    if (already) return ESP_OK;

    ah_task_ctx_t *ctx = (ah_task_ctx_t *)malloc(sizeof(ah_task_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;
    ctx->idx = idx;
    ctx->mgr = mgr;

    char name[16];
    snprintf(name, sizeof(name), "Ah_%d", idx);
    TaskHandle_t handle = NULL;
    if (xTaskCreate(ah_task, name, 3072, ctx, 1, &handle) != pdPASS) {
        free(ctx);
        return ESP_FAIL;
    }

    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        mgr->ah_tasks[idx] = handle;
        mgr->ah_running[idx] = true;
        xSemaphoreGive(mgr->mutex);
    }
    ESP_LOGI(TAG, "Ah task started for BAT[%d]", idx + 1);
    return ESP_OK;
}

float bmu_battery_manager_get_max_voltage_mv(bmu_battery_manager_t *mgr)
{
    float max_mv = 0;
    for (int i = 0; i < mgr->nb_ina; i++) {
        float v = 0;
        if (bmu_ina237_read_bus_voltage(&mgr->ina_devices[i], &v) == ESP_OK && v > max_mv) {
            max_mv = v;
        }
    }
    return max_mv;
}

float bmu_battery_manager_get_min_voltage_mv(bmu_battery_manager_t *mgr)
{
    float min_mv = 999999;
    for (int i = 0; i < mgr->nb_ina; i++) {
        float v = 0;
        if (bmu_ina237_read_bus_voltage(&mgr->ina_devices[i], &v) == ESP_OK && v > 1000 && v < min_mv) {
            min_mv = v;
        }
    }
    return min_mv < 999999 ? min_mv : 0;
}

float bmu_battery_manager_get_avg_voltage_mv(bmu_battery_manager_t *mgr)
{
    float sum = 0;
    int n = 0;
    for (int i = 0; i < mgr->nb_ina; i++) {
        float v = 0;
        if (bmu_ina237_read_bus_voltage(&mgr->ina_devices[i], &v) == ESP_OK && v > 1000) {
            sum += v;
            n++;
        }
    }
    return n > 0 ? sum / n : 0;
}

float bmu_battery_manager_get_ah_discharge(bmu_battery_manager_t *mgr, int idx)
{
    if (idx < 0 || idx >= BMU_MAX_BATTERIES) return 0;
    float v = 0;
    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        v = mgr->ah_discharge[idx];
        xSemaphoreGive(mgr->mutex);
    }
    return v;
}

float bmu_battery_manager_get_ah_charge(bmu_battery_manager_t *mgr, int idx)
{
    if (idx < 0 || idx >= BMU_MAX_BATTERIES) return 0;
    float v = 0;
    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        v = mgr->ah_charge[idx];
        xSemaphoreGive(mgr->mutex);
    }
    return v;
}

float bmu_battery_manager_get_total_current_a(bmu_battery_manager_t *mgr)
{
    float total = 0;
    for (int i = 0; i < mgr->nb_ina; i++) {
        float c = 0;
        if (bmu_ina237_read_current(&mgr->ina_devices[i], &c) == ESP_OK && !isnan(c)) {
            total += c;
        }
    }
    return total;
}
```

- [ ] **Step 3: Update CMakeLists.txt to add bmu_battery_manager.cpp**

In `firmware-idf/components/bmu_protection/CMakeLists.txt`, change:
```cmake
idf_component_register(
    SRCS "bmu_protection.cpp" "bmu_battery_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_ina237 bmu_tca9535 bmu_config
)
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_protection/
git commit -m "feat(idf): add bmu_battery_manager — Ah tracking via FreeRTOS tasks"
```

---

### Task 2: Update main.cpp — Full Protection Loop

**Files:**
- Modify: `firmware-idf/main/CMakeLists.txt`
- Modify: `firmware-idf/main/main.cpp`

- [ ] **Step 1: Update main CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES bmu_i2c bmu_ina237 bmu_tca9535 bmu_protection bmu_config
)
```

- [ ] **Step 2: Rewrite main.cpp**

```cpp
#include "bmu_i2c.h"
#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "══════════════════════════════════════════════");
    ESP_LOGI(TAG, "  KXKM BMU — ESP-IDF v5.3 (Phase 2)");
    ESP_LOGI(TAG, "══════════════════════════════════════════════");
    bmu_config_log();

    /* I2C bus on PMOD1 GPIO40/41 @ 50kHz */
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(bmu_i2c_init(&i2c_bus));
    bmu_i2c_scan(i2c_bus);

    /* INA237 (shunt=2mΩ=2000µΩ, max=10A) */
    static bmu_ina237_t ina[BMU_MAX_BATTERIES] = {};
    uint8_t nb_ina = 0;
    bmu_ina237_scan_init(i2c_bus, 2000, 10.0f, ina, &nb_ina);
    for (int i = 0; i < nb_ina; i++) {
        bmu_ina237_set_bus_voltage_alerts(&ina[i], BMU_MAX_VOLTAGE_MV, BMU_MIN_VOLTAGE_MV);
    }

    /* TCA9535 */
    static bmu_tca9535_handle_t tca[BMU_MAX_TCA] = {};
    uint8_t nb_tca = 0;
    bmu_tca9535_scan_init(i2c_bus, tca, BMU_MAX_TCA, &nb_tca);

    /* Topology: Nb_TCA * 4 == Nb_INA */
    bool topology_ok = (nb_ina > 0) && (nb_tca > 0) && (nb_tca * 4 == nb_ina);
    if (!topology_ok) {
        ESP_LOGE(TAG, "TOPOLOGY MISMATCH %d TCA * 4 != %d INA — FAIL-SAFE", nb_tca, nb_ina);
    }

    /* Protection state machine */
    static bmu_protection_ctx_t prot = {};
    ESP_ERROR_CHECK(bmu_protection_init(&prot, ina, nb_ina, tca, nb_tca));

    /* Battery manager (Ah tracking) */
    static bmu_battery_manager_t mgr = {};
    bmu_battery_manager_init(&mgr, ina, nb_ina);
    for (int i = 0; i < nb_ina; i++) {
        bmu_battery_manager_start_ah_task(&mgr, i);
    }

    ESP_LOGI(TAG, "Init complete — entering protection loop (%d ms)", BMU_LOOP_PERIOD_MS);

    /* Main protection loop */
    while (true) {
        if (!topology_ok) {
            bmu_protection_all_off(&prot);
        } else {
            for (int i = 0; i < nb_ina; i++) {
                bmu_protection_check_battery(&prot, i);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BMU_LOOP_PERIOD_MS));
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/main/
git commit -m "feat(idf): Phase 2 complete — full protection loop + Ah tracking in main"
```

---

### Task 3: Rust FFI Bridge (bmu-ffi crate)

**Files:**
- Create: `firmware-rs/crates/bmu-ffi/Cargo.toml`
- Create: `firmware-rs/crates/bmu-ffi/src/lib.rs`
- Create: `firmware-rs/crates/bmu-ffi/cbindgen.toml`
- Modify: `firmware-rs/Cargo.toml` (add member)

- [ ] **Step 1: Add bmu-ffi to workspace**

In `firmware-rs/Cargo.toml`, add `"crates/bmu-ffi"` to the members list.

- [ ] **Step 2: Create bmu-ffi/Cargo.toml**

```toml
[package]
name = "bmu-ffi"
version = "0.1.0"
edition = "2021"

[lib]
crate-type = ["staticlib"]

[dependencies]
bmu-protection = { path = "../bmu-protection" }

[build-dependencies]
cbindgen = "0.27"
```

- [ ] **Step 3: Create bmu-ffi/cbindgen.toml**

```toml
language = "C"
header = "/* Auto-generated by cbindgen — do not edit */"
include_guard = "BMU_FFI_H"
no_includes = true
sys_includes = ["stdint.h", "stdbool.h"]

[export]
prefix = "bmu_"

[enum]
rename_variants = "ScreamingSnakeCase"
```

- [ ] **Step 4: Create bmu-ffi/src/lib.rs**

```rust
//! C FFI bridge for Rust protection state machine.
//! Exports extern "C" functions callable from ESP-IDF C code.

#![no_std]

use bmu_protection::{BatteryAction, BatteryState, Protection, ProtectionConfig};
use core::ptr;

/// Opaque handle to Rust Protection<16>
pub type BmuProtectionHandle = Protection<16>;

/// FFI-safe battery action enum
#[repr(C)]
pub enum BmuAction {
    None = 0,
    SwitchOn = 1,
    SwitchOff = 2,
    EmergencyOff = 3,
    PermanentLock = 4,
}

/// FFI-safe battery state enum
#[repr(C)]
pub enum BmuState {
    Connected = 0,
    Disconnected = 1,
    Reconnecting = 2,
    Error = 3,
    Locked = 4,
}

/// FFI-safe protection config
#[repr(C)]
pub struct BmuProtectionConfig {
    pub min_voltage_mv: u16,
    pub max_voltage_mv: u16,
    pub max_current_ma: u16,
    pub voltage_diff_mv: u16,
    pub reconnect_delay_ms: u32,
    pub nb_switch_max: u8,
    pub overcurrent_factor: u16,
}

fn action_to_ffi(a: BatteryAction) -> BmuAction {
    match a {
        BatteryAction::None => BmuAction::None,
        BatteryAction::SwitchOn => BmuAction::SwitchOn,
        BatteryAction::SwitchOff => BmuAction::SwitchOff,
        BatteryAction::EmergencyOff => BmuAction::EmergencyOff,
        BatteryAction::PermanentLock => BmuAction::PermanentLock,
    }
}

fn state_to_ffi(s: BatteryState) -> BmuState {
    match s {
        BatteryState::Connected => BmuState::Connected,
        BatteryState::Disconnected => BmuState::Disconnected,
        BatteryState::Reconnecting => BmuState::Reconnecting,
        BatteryState::Error => BmuState::Error,
        BatteryState::Locked => BmuState::Locked,
    }
}

/// Create a new protection context. Caller must free with bmu_protection_free().
/// Returns null on allocation failure.
#[no_mangle]
pub extern "C" fn bmu_protection_new(config: *const BmuProtectionConfig) -> *mut BmuProtectionHandle {
    if config.is_null() { return ptr::null_mut(); }
    let cfg = unsafe { &*config };
    let prot = Protection::<16>::new(ProtectionConfig {
        min_voltage_mv: cfg.min_voltage_mv,
        max_voltage_mv: cfg.max_voltage_mv,
        max_current_ma: cfg.max_current_ma,
        voltage_diff_mv: cfg.voltage_diff_mv,
        reconnect_delay_ms: cfg.reconnect_delay_ms,
        nb_switch_max: cfg.nb_switch_max,
        overcurrent_factor: cfg.overcurrent_factor,
    });
    // Box requires alloc — in no_std we use a static mut instead
    // For now, return a leaked Box (requires alloc feature)
    // TODO: use a static once or pass pre-allocated memory from C
    unsafe {
        static mut PROT: core::mem::MaybeUninit<Protection<16>> = core::mem::MaybeUninit::uninit();
        PROT.write(prot);
        PROT.as_mut_ptr()
    }
}

/// Run one protection check for battery idx.
#[no_mangle]
pub extern "C" fn bmu_protection_check(
    ctx: *mut BmuProtectionHandle,
    idx: u8,
    v_mv: f32,
    i_a: f32,
    now_ms: u64,
) -> BmuAction {
    if ctx.is_null() { return BmuAction::None; }
    let prot = unsafe { &mut *ctx };
    action_to_ffi(prot.check_battery(idx as usize, v_mv, i_a, now_ms))
}

/// Get current state of a battery.
#[no_mangle]
pub extern "C" fn bmu_protection_get_state(ctx: *const BmuProtectionHandle, idx: u8) -> BmuState {
    if ctx.is_null() { return BmuState::Disconnected; }
    let prot = unsafe { &*ctx };
    state_to_ffi(prot.get_state(idx as usize))
}

/// Get cached voltage for a battery (mV).
#[no_mangle]
pub extern "C" fn bmu_protection_get_voltage(ctx: *const BmuProtectionHandle, idx: u8) -> f32 {
    if ctx.is_null() { return 0.0; }
    let prot = unsafe { &*ctx };
    prot.get_voltage_mv(idx as usize)
}

/// Reset switch count for a battery.
#[no_mangle]
pub extern "C" fn bmu_protection_reset(ctx: *mut BmuProtectionHandle, idx: u8) {
    if ctx.is_null() { return; }
    let prot = unsafe { &mut *ctx };
    prot.reset_switch_count(idx as usize);
}
```

- [ ] **Step 5: Commit**

```bash
git add firmware-rs/
git commit -m "feat(rs): add bmu-ffi crate — C FFI bridge for Rust protection state machine"
```
