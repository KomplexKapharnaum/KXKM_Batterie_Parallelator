# Internal Resistance Measurement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add active internal resistance measurement (R_ohmic + R_polarization) to the BMU via controlled MOSFET pulse-OFF sequences, with three trigger modes and context-aware output routing.

**Architecture:** New ESP-IDF component `bmu_rint` owns measurement sequence, guards, caching, and output routing. Triggers (opportunistic, periodic, on-demand) call into it. Integration points: `bmu_protection` (opportunistic hook), `bmu_web` (REST API), `bmu_ble_battery_svc` (BLE chars), `bmu_display` (LVGL debug), `bmu_soh` (feature replacement).

**Tech Stack:** ESP-IDF 5.4, C/C++, Unity test framework, INA237 (V/I), TCA9535 (MOSFET switch), NimBLE, esp_http_server, NVS, LVGL

---

## File Structure

| Action | Path | Responsibility |
|--------|------|----------------|
| Create | `firmware-idf/components/bmu_rint/include/bmu_rint.h` | Public API, types, result struct |
| Create | `firmware-idf/components/bmu_rint/bmu_rint.cpp` | Measurement sequence, guards, caching, periodic task |
| Create | `firmware-idf/components/bmu_rint/bmu_rint_output.cpp` | MQTT/NVS/display output routing |
| Create | `firmware-idf/components/bmu_rint/CMakeLists.txt` | Component build config |
| Create | `firmware-idf/components/bmu_rint/Kconfig` | Runtime configuration |
| Create | `firmware-idf/test/test_rint/CMakeLists.txt` | Test build |
| Create | `firmware-idf/test/test_rint/main/CMakeLists.txt` | Test main build |
| Create | `firmware-idf/test/test_rint/main/test_rint.cpp` | Host unit tests |
| Modify | `firmware-idf/components/bmu_protection/bmu_protection.cpp` | Opportunistic hook on disconnect |
| Modify | `firmware-idf/components/bmu_web/bmu_web.cpp` | REST API endpoints |
| Modify | `firmware-idf/components/bmu_ble/bmu_ble_battery_svc.cpp` | BLE RINT characteristics |
| Modify | `firmware-idf/components/bmu_display/bmu_ui_debug.cpp` | R_int display section |
| Modify | `firmware-idf/components/bmu_soh/bmu_soh.cpp` | Replace r_int fallback |
| Modify | `firmware-idf/main/main.cpp` | Init + periodic start |
| Modify | `firmware-idf/sdkconfig.defaults` | Default Kconfig values |

---

### Task 1: Host tests — R_int computation and validation

**Files:**
- Create: `firmware-idf/test/test_rint/CMakeLists.txt`
- Create: `firmware-idf/test/test_rint/main/CMakeLists.txt`
- Create: `firmware-idf/test/test_rint/main/test_rint.cpp`

- [ ] **Step 1: Create test build files**

Create `firmware-idf/test/test_rint/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_rint)
```

Create `firmware-idf/test/test_rint/main/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "test_rint.cpp"
    INCLUDE_DIRS "."
    REQUIRES unity
)
```

- [ ] **Step 2: Write failing tests for R_int computation**

Create `firmware-idf/test/test_rint/main/test_rint.cpp`:
```cpp
#include <unity.h>
#include <cmath>
#include <cstdint>
#include <cstring>

// ── Types (mirrored from bmu_rint.h — keep in sync) ──────────────────

typedef enum {
    BMU_RINT_TRIGGER_OPPORTUNISTIC,
    BMU_RINT_TRIGGER_PERIODIC,
    BMU_RINT_TRIGGER_ON_DEMAND,
} bmu_rint_trigger_t;

typedef struct {
    float   r_ohmic_mohm;
    float   r_total_mohm;
    float   v_load_mv;
    float   v_ocv_fast_mv;
    float   v_ocv_stable_mv;
    float   i_load_a;
    int64_t timestamp_ms;
    bool    valid;
} bmu_rint_result_t;

// ── Pure logic under test ─────────────────────────────────────────────

static const float RINT_MIN_CURRENT_A    = 0.5f;
static const float RINT_R_MAX_MOHM       = 500.0f;
static const int   RINT_MIN_CONNECTED    = 2;

static bool rint_guard_check(int nb_connected, bool has_error_or_locked,
                             bool target_connected, bool measurement_active)
{
    if (nb_connected < RINT_MIN_CONNECTED) return false;
    if (has_error_or_locked)               return false;
    if (!target_connected)                 return false;
    if (measurement_active)                return false;
    return true;
}

static bmu_rint_result_t rint_compute(float v_load_mv, float i_load_a,
                                       float v_ocv_fast_mv, float v_ocv_stable_mv,
                                       int64_t timestamp_ms)
{
    bmu_rint_result_t r = {};
    r.v_load_mv       = v_load_mv;
    r.v_ocv_fast_mv   = v_ocv_fast_mv;
    r.v_ocv_stable_mv = v_ocv_stable_mv;
    r.i_load_a        = i_load_a;
    r.timestamp_ms    = timestamp_ms;
    r.valid            = false;

    // Guard: minimum load current
    if (fabsf(i_load_a) < RINT_MIN_CURRENT_A) return r;

    // Compute R values (ΔV / I, result in mΩ)
    float dv_fast_mv   = v_ocv_fast_mv - v_load_mv;
    float dv_stable_mv = v_ocv_stable_mv - v_load_mv;

    // Sanity: voltage must rise when disconnected
    if (dv_fast_mv <= 0.0f) return r;

    float r_ohmic = dv_fast_mv / fabsf(i_load_a);
    float r_total = dv_stable_mv / fabsf(i_load_a);

    // Plausibility
    if (r_ohmic <= 0.0f || r_ohmic > RINT_R_MAX_MOHM) return r;
    if (r_total < r_ohmic) return r;

    r.r_ohmic_mohm = r_ohmic;
    r.r_total_mohm = r_total;
    r.valid        = true;
    return r;
}

static void rint_format_influx(char *buf, size_t len, int battery_idx,
                                bmu_rint_trigger_t trigger,
                                const bmu_rint_result_t *res)
{
    const char *trigger_str = "unknown";
    switch (trigger) {
        case BMU_RINT_TRIGGER_OPPORTUNISTIC: trigger_str = "opportunistic"; break;
        case BMU_RINT_TRIGGER_PERIODIC:      trigger_str = "periodic";      break;
        case BMU_RINT_TRIGGER_ON_DEMAND:     trigger_str = "on_demand";     break;
    }
    float r_polar = res->r_total_mohm - res->r_ohmic_mohm;
    snprintf(buf, len,
             "rint,battery=%d,trigger=%s "
             "r_ohmic_mohm=%.2f,r_total_mohm=%.2f,r_polar_mohm=%.2f,"
             "v_load_mv=%.1f,v_ocv_fast_mv=%.1f,v_ocv_stable_mv=%.1f,"
             "i_load_a=%.3f",
             battery_idx, trigger_str,
             res->r_ohmic_mohm, res->r_total_mohm, r_polar,
             res->v_load_mv, res->v_ocv_fast_mv, res->v_ocv_stable_mv,
             res->i_load_a);
}

static void rint_format_json(char *buf, size_t len, int battery_idx,
                              const bmu_rint_result_t *res)
{
    float r_polar = res->r_total_mohm - res->r_ohmic_mohm;
    snprintf(buf, len,
             "{\"index\":%d,\"r_ohmic_mohm\":%.2f,\"r_total_mohm\":%.2f,"
             "\"r_polar_mohm\":%.2f,\"v_load_mv\":%.1f,"
             "\"v_ocv_stable_mv\":%.1f,\"i_load_a\":%.3f,"
             "\"timestamp\":%lld,\"valid\":%s}",
             battery_idx, res->r_ohmic_mohm, res->r_total_mohm, r_polar,
             res->v_load_mv, res->v_ocv_stable_mv, res->i_load_a,
             (long long)res->timestamp_ms,
             res->valid ? "true" : "false");
}

// ── Guard tests ───────────────────────────────────────────────────────

void test_guard_passes_normal(void)
{
    TEST_ASSERT_TRUE(rint_guard_check(4, false, true, false));
}

void test_guard_fails_too_few_connected(void)
{
    TEST_ASSERT_FALSE(rint_guard_check(1, false, true, false));
}

void test_guard_fails_exactly_one_connected(void)
{
    TEST_ASSERT_FALSE(rint_guard_check(1, false, true, false));
}

void test_guard_passes_exactly_two_connected(void)
{
    TEST_ASSERT_TRUE(rint_guard_check(2, false, true, false));
}

void test_guard_fails_error_state(void)
{
    TEST_ASSERT_FALSE(rint_guard_check(8, true, true, false));
}

void test_guard_fails_target_disconnected(void)
{
    TEST_ASSERT_FALSE(rint_guard_check(8, false, false, false));
}

void test_guard_fails_measurement_active(void)
{
    TEST_ASSERT_FALSE(rint_guard_check(8, false, true, true));
}

// ── Computation tests ─────────────────────────────────────────────────

void test_compute_normal_measurement(void)
{
    // V_load=26000mV, I=3A, V_fast=26045mV (+45mV), V_stable=26072mV (+72mV)
    // R_ohmic = 45/3 = 15 mΩ, R_total = 72/3 = 24 mΩ
    bmu_rint_result_t r = rint_compute(26000.0f, 3.0f, 26045.0f, 26072.0f, 1000);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 15.0f, r.r_ohmic_mohm);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 24.0f, r.r_total_mohm);
}

void test_compute_low_current_rejected(void)
{
    // I=0.2A < 0.5A threshold
    bmu_rint_result_t r = rint_compute(26000.0f, 0.2f, 26010.0f, 26020.0f, 1000);
    TEST_ASSERT_FALSE(r.valid);
}

void test_compute_negative_dv_rejected(void)
{
    // V_fast < V_load — voltage dropped instead of rising
    bmu_rint_result_t r = rint_compute(26000.0f, 3.0f, 25990.0f, 26010.0f, 1000);
    TEST_ASSERT_FALSE(r.valid);
}

void test_compute_excessive_r_rejected(void)
{
    // V_fast = 26000 + 2000 = 28000 → R = 2000/3 = 666 mΩ > 500 max
    bmu_rint_result_t r = rint_compute(26000.0f, 3.0f, 28000.0f, 28500.0f, 1000);
    TEST_ASSERT_FALSE(r.valid);
}

void test_compute_r_total_less_than_r_ohmic_rejected(void)
{
    // V_stable < V_fast — impossible, polarization can't be negative
    bmu_rint_result_t r = rint_compute(26000.0f, 3.0f, 26060.0f, 26030.0f, 1000);
    TEST_ASSERT_FALSE(r.valid);
}

void test_compute_negative_current_uses_abs(void)
{
    // Negative current (charging) — should use |I|
    bmu_rint_result_t r = rint_compute(26000.0f, -3.0f, 26045.0f, 26072.0f, 1000);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 15.0f, r.r_ohmic_mohm);
}

void test_compute_high_current_small_dv(void)
{
    // 10A load, 5mV rise → R_ohmic = 0.5 mΩ (good connection)
    bmu_rint_result_t r = rint_compute(26000.0f, 10.0f, 26005.0f, 26008.0f, 1000);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.5f, r.r_ohmic_mohm);
}

void test_compute_boundary_current_exact_threshold(void)
{
    // I=0.5A exactly — should pass
    bmu_rint_result_t r = rint_compute(26000.0f, 0.5f, 26010.0f, 26020.0f, 1000);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 20.0f, r.r_ohmic_mohm);
}

void test_compute_preserves_raw_values(void)
{
    bmu_rint_result_t r = rint_compute(25500.0f, 5.0f, 25530.0f, 25560.0f, 42000);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25500.0f, r.v_load_mv);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25530.0f, r.v_ocv_fast_mv);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 25560.0f, r.v_ocv_stable_mv);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, r.i_load_a);
    TEST_ASSERT_EQUAL_INT64(42000, r.timestamp_ms);
}

// ── Formatting tests ──────────────────────────────────────────────────

void test_influx_format_periodic(void)
{
    bmu_rint_result_t res = {
        .r_ohmic_mohm = 15.0f, .r_total_mohm = 24.0f,
        .v_load_mv = 26000.0f, .v_ocv_fast_mv = 26045.0f,
        .v_ocv_stable_mv = 26072.0f, .i_load_a = 3.0f,
        .timestamp_ms = 1000, .valid = true
    };
    char buf[256];
    rint_format_influx(buf, sizeof(buf), 0, BMU_RINT_TRIGGER_PERIODIC, &res);
    // Check measurement + tag
    TEST_ASSERT_NOT_NULL(strstr(buf, "rint,battery=0,trigger=periodic"));
    // Check fields
    TEST_ASSERT_NOT_NULL(strstr(buf, "r_ohmic_mohm=15.00"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "r_total_mohm=24.00"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "r_polar_mohm=9.00"));
}

void test_influx_format_opportunistic(void)
{
    bmu_rint_result_t res = {
        .r_ohmic_mohm = 10.0f, .r_total_mohm = 18.0f,
        .v_load_mv = 27000.0f, .v_ocv_fast_mv = 27020.0f,
        .v_ocv_stable_mv = 27036.0f, .i_load_a = 2.0f,
        .timestamp_ms = 5000, .valid = true
    };
    char buf[256];
    rint_format_influx(buf, sizeof(buf), 5, BMU_RINT_TRIGGER_OPPORTUNISTIC, &res);
    TEST_ASSERT_NOT_NULL(strstr(buf, "battery=5,trigger=opportunistic"));
}

void test_json_format_valid(void)
{
    bmu_rint_result_t res = {
        .r_ohmic_mohm = 12.5f, .r_total_mohm = 20.0f,
        .v_load_mv = 26500.0f, .v_ocv_fast_mv = 26537.5f,
        .v_ocv_stable_mv = 26560.0f, .i_load_a = 3.0f,
        .timestamp_ms = 9999, .valid = true
    };
    char buf[512];
    rint_format_json(buf, sizeof(buf), 2, &res);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"index\":2"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"r_ohmic_mohm\":12.50"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"valid\":true"));
}

void test_json_format_invalid(void)
{
    bmu_rint_result_t res = {};
    res.valid = false;
    char buf[512];
    rint_format_json(buf, sizeof(buf), 0, &res);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"valid\":false"));
}

// ── Entry point ───────────────────────────────────────────────────────

void setUp(void) {}
void tearDown(void) {}

int main(void)
{
    UNITY_BEGIN();

    // Guards (7 tests)
    RUN_TEST(test_guard_passes_normal);
    RUN_TEST(test_guard_fails_too_few_connected);
    RUN_TEST(test_guard_fails_exactly_one_connected);
    RUN_TEST(test_guard_passes_exactly_two_connected);
    RUN_TEST(test_guard_fails_error_state);
    RUN_TEST(test_guard_fails_target_disconnected);
    RUN_TEST(test_guard_fails_measurement_active);

    // Computation (8 tests)
    RUN_TEST(test_compute_normal_measurement);
    RUN_TEST(test_compute_low_current_rejected);
    RUN_TEST(test_compute_negative_dv_rejected);
    RUN_TEST(test_compute_excessive_r_rejected);
    RUN_TEST(test_compute_r_total_less_than_r_ohmic_rejected);
    RUN_TEST(test_compute_negative_current_uses_abs);
    RUN_TEST(test_compute_high_current_small_dv);
    RUN_TEST(test_compute_boundary_current_exact_threshold);
    RUN_TEST(test_compute_preserves_raw_values);

    // Formatting (4 tests)
    RUN_TEST(test_influx_format_periodic);
    RUN_TEST(test_influx_format_opportunistic);
    RUN_TEST(test_json_format_valid);
    RUN_TEST(test_json_format_invalid);

    return UNITY_END();
}
```

- [ ] **Step 3: Run tests to verify they compile and pass**

Run: `cd firmware-idf && idf.py -C test/test_rint build 2>&1 | tail -5`
Then: `cd firmware-idf && idf.py -C test/test_rint flash monitor` (on target)
Or for host: build with CMake targeting linux host if available.

Expected: 20 tests PASS (all logic is self-contained in the test file)

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/test/test_rint/
git commit -m "test(rint): host tests for R_int computation"
```

---

### Task 2: Component scaffold — header, Kconfig, CMakeLists

**Files:**
- Create: `firmware-idf/components/bmu_rint/include/bmu_rint.h`
- Create: `firmware-idf/components/bmu_rint/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_rint/Kconfig`

- [ ] **Step 1: Create the public header**

Create `firmware-idf/components/bmu_rint/include/bmu_rint.h`:
```cpp
#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMU_MAX_BATTERIES 32

typedef enum {
    BMU_RINT_TRIGGER_OPPORTUNISTIC,
    BMU_RINT_TRIGGER_PERIODIC,
    BMU_RINT_TRIGGER_ON_DEMAND,
} bmu_rint_trigger_t;

typedef struct {
    float   r_ohmic_mohm;       // R₀ ohmique (mΩ) — at +100 ms
    float   r_total_mohm;       // R₀ + R₁ total (mΩ) — at +1 s
    float   v_load_mv;          // V₁ under load (mV)
    float   v_ocv_fast_mv;      // V₂ at +PULSE_FAST_MS (mV)
    float   v_ocv_stable_mv;    // V₃ at +PULSE_TOTAL_MS (mV)
    float   i_load_a;           // I₁ at measurement start (A)
    int64_t timestamp_ms;       // Measurement time (epoch ms or uptime ms)
    bool    valid;              // Measurement passed all checks
} bmu_rint_result_t;

/**
 * Initialise le composant bmu_rint.
 * Charge les dernières valeurs NVS en cache.
 * Appeler une seule fois au boot, après bmu_protection_init.
 */
esp_err_t bmu_rint_init(void);

/**
 * Mesure active sur une batterie (bloque ~1.2 s).
 * Vérifie les guards avant d'agir.
 * @return ESP_OK si mesure valide, ESP_ERR_INVALID_STATE si guards échouent
 */
esp_err_t bmu_rint_measure(uint8_t battery_idx, bmu_rint_trigger_t trigger);

/**
 * Mesure séquentielle de toutes les batteries éligibles.
 */
esp_err_t bmu_rint_measure_all(bmu_rint_trigger_t trigger);

/**
 * Résultat en cache (dernière mesure valide).
 */
bmu_rint_result_t bmu_rint_get_cached(uint8_t battery_idx);

/**
 * Hook opportuniste — appeler depuis bmu_protection sur déconnexion naturelle.
 * Ne coupe PAS le MOSFET (déjà coupé). Lit V₂/V₃ après la coupure.
 */
void bmu_rint_on_disconnect(uint8_t battery_idx, float v_before_mv, float i_before_a);

/**
 * Démarre la tâche périodique (si CONFIG_BMU_RINT_PERIODIC_ENABLED).
 */
esp_err_t bmu_rint_start_periodic(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Create Kconfig**

Create `firmware-idf/components/bmu_rint/Kconfig`:
```
menu "BMU Internal Resistance"

    config BMU_RINT_ENABLED
        bool "Enable R_int measurement"
        default y

    config BMU_RINT_PERIODIC_ENABLED
        bool "Enable periodic R_int measurement"
        default y
        depends on BMU_RINT_ENABLED

    config BMU_RINT_PERIOD_MIN
        int "Periodic measurement interval (minutes)"
        default 30
        range 5 1440
        depends on BMU_RINT_PERIODIC_ENABLED

    config BMU_RINT_MIN_CURRENT_MA
        int "Minimum load current for valid measurement (mA)"
        default 500
        range 100 5000
        depends on BMU_RINT_ENABLED

    config BMU_RINT_PULSE_FAST_MS
        int "Delay before V2 read — ohmique (ms)"
        default 100
        range 50 500
        depends on BMU_RINT_ENABLED

    config BMU_RINT_PULSE_TOTAL_MS
        int "Total pulse OFF duration (ms)"
        default 1000
        range 500 5000
        depends on BMU_RINT_ENABLED

    config BMU_RINT_R_MAX_MOHM
        int "Maximum plausible R_int (mohm)"
        default 500
        range 100 2000
        depends on BMU_RINT_ENABLED

    config BMU_RINT_DISPLAY_WARN_MOHM
        int "Display warning threshold (mohm, orange)"
        default 50
        depends on BMU_RINT_ENABLED

    config BMU_RINT_DISPLAY_CRIT_MOHM
        int "Display critical threshold (mohm, red)"
        default 100
        depends on BMU_RINT_ENABLED

endmenu
```

- [ ] **Step 3: Create CMakeLists.txt**

Create `firmware-idf/components/bmu_rint/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "bmu_rint.cpp" "bmu_rint_output.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_ina237 bmu_tca9535 bmu_protection
    PRIV_REQUIRES bmu_config bmu_i2c nvs_flash esp_timer
)
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_rint/
git commit -m "feat(rint): component scaffold — header, Kconfig"
```

---

### Task 3: Core measurement — guards, sequence, caching

**Files:**
- Create: `firmware-idf/components/bmu_rint/bmu_rint.cpp`

- [ ] **Step 1: Write bmu_rint.cpp**

Create `firmware-idf/components/bmu_rint/bmu_rint.cpp`:
```cpp
#include "bmu_rint.h"
#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "bmu_protection.h"
#include "bmu_config.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cmath>
#include <cstring>

static const char *TAG = "bmu_rint";

// ── State ─────────────────────────────────────────────────────────────

static bmu_rint_result_t s_cache[BMU_MAX_BATTERIES] = {};
static SemaphoreHandle_t s_mutex     = nullptr;
static bool              s_measuring = false;
static TaskHandle_t      s_periodic_task = nullptr;

// Pointeurs injectés via init (set by main.cpp extern linkage)
extern bmu_protection_ctx_t *g_prot;
extern bmu_ina237_t         *g_ina;
extern bmu_tca9535_handle_t *g_tca;
extern uint8_t               g_nb_ina;
extern uint8_t               g_nb_tca;

// ── Helpers ───────────────────────────────────────────────────────────

static int count_connected(void)
{
    int n = 0;
    for (int i = 0; i < g_nb_ina; i++) {
        if (bmu_protection_get_state(g_prot, i) == BMU_STATE_CONNECTED) {
            n++;
        }
    }
    return n;
}

static bool has_error_or_locked(void)
{
    for (int i = 0; i < g_nb_ina; i++) {
        bmu_battery_state_t st = bmu_protection_get_state(g_prot, i);
        if (st == BMU_STATE_ERROR || st == BMU_STATE_LOCKED) return true;
    }
    return false;
}

static bool guard_check(uint8_t idx)
{
    if (idx >= g_nb_ina) return false;
    if (count_connected() < 2) return false;
    if (has_error_or_locked()) return false;
    if (bmu_protection_get_state(g_prot, idx) != BMU_STATE_CONNECTED) return false;
    if (s_measuring) return false;
    return true;
}

static bmu_rint_result_t compute_result(float v_load, float i_load,
                                         float v_fast, float v_stable,
                                         int64_t ts)
{
    bmu_rint_result_t r = {};
    r.v_load_mv       = v_load;
    r.v_ocv_fast_mv   = v_fast;
    r.v_ocv_stable_mv = v_stable;
    r.i_load_a        = i_load;
    r.timestamp_ms    = ts;
    r.valid            = false;

    float min_a = CONFIG_BMU_RINT_MIN_CURRENT_MA / 1000.0f;
    if (fabsf(i_load) < min_a) return r;

    float dv_fast   = v_fast - v_load;
    float dv_stable = v_stable - v_load;
    if (dv_fast <= 0.0f) return r;

    float r_ohmic = dv_fast / fabsf(i_load);
    float r_total = dv_stable / fabsf(i_load);

    if (r_ohmic <= 0.0f || r_ohmic > (float)CONFIG_BMU_RINT_R_MAX_MOHM) return r;
    if (r_total < r_ohmic) return r;

    r.r_ohmic_mohm = r_ohmic;
    r.r_total_mohm = r_total;
    r.valid        = true;
    return r;
}

// ── Output routing (defined in bmu_rint_output.cpp) ───────────────────

extern void rint_output_route(uint8_t idx, bmu_rint_trigger_t trigger,
                               const bmu_rint_result_t *res);

// ── Public API ────────────────────────────────────────────────────────

esp_err_t bmu_rint_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    memset(s_cache, 0, sizeof(s_cache));
    // NVS load of cached values is done in bmu_rint_output.cpp
    ESP_LOGI(TAG, "bmu_rint init OK");
    return ESP_OK;
}

esp_err_t bmu_rint_measure(uint8_t battery_idx, bmu_rint_trigger_t trigger)
{
    if (!guard_check(battery_idx)) {
        ESP_LOGW(TAG, "BAT[%d] guard check failed", battery_idx + 1);
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    s_measuring = true;
    xSemaphoreGive(s_mutex);

    int tca_idx = battery_idx / 4;
    int channel = battery_idx % 4;
    int64_t now_ms = esp_timer_get_time() / 1000;

    // Step 1: Read V₁, I₁ sous charge
    float v1 = 0, i1 = 0;
    esp_err_t ret = bmu_ina237_read_voltage_current(&g_ina[battery_idx], &v1, &i1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BAT[%d] V1/I1 read failed", battery_idx + 1);
        goto cleanup;
    }

    ESP_LOGI(TAG, "BAT[%d] pulse OFF — V1=%.0fmV I1=%.3fA",
             battery_idx + 1, v1, i1);

    // Step 2: MOSFET OFF
    ret = bmu_tca9535_switch_battery(&g_tca[tca_idx], channel, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BAT[%d] switch OFF failed", battery_idx + 1);
        goto cleanup;
    }

    {
        // Step 3: Wait + read V₂ (ohmique)
        vTaskDelay(pdMS_TO_TICKS(CONFIG_BMU_RINT_PULSE_FAST_MS));

        // Check abort: if protection flagged another battery, stop now
        if (has_error_or_locked()) {
            ESP_LOGW(TAG, "BAT[%d] abort — error detected during pulse", battery_idx + 1);
            bmu_tca9535_switch_battery(&g_tca[tca_idx], channel, true);
            ret = ESP_ERR_INVALID_STATE;
            goto cleanup;
        }

        float v2 = 0, i2_unused = 0;
        ret = bmu_ina237_read_voltage_current(&g_ina[battery_idx], &v2, &i2_unused);
        if (ret != ESP_OK) {
            bmu_tca9535_switch_battery(&g_tca[tca_idx], channel, true);
            goto cleanup;
        }

        // Step 4: Wait remaining time + read V₃ (total)
        int remaining_ms = CONFIG_BMU_RINT_PULSE_TOTAL_MS - CONFIG_BMU_RINT_PULSE_FAST_MS;
        if (remaining_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(remaining_ms));
        }

        // Abort check again
        if (has_error_or_locked()) {
            ESP_LOGW(TAG, "BAT[%d] abort — error detected during pulse", battery_idx + 1);
            bmu_tca9535_switch_battery(&g_tca[tca_idx], channel, true);
            ret = ESP_ERR_INVALID_STATE;
            goto cleanup;
        }

        float v3 = 0, i3_unused = 0;
        ret = bmu_ina237_read_voltage_current(&g_ina[battery_idx], &v3, &i3_unused);

        // Step 5: MOSFET ON (always, even if v3 read failed)
        bmu_tca9535_switch_battery(&g_tca[tca_idx], channel, true);

        if (ret != ESP_OK) goto cleanup;

        // Step 6: Compute and cache
        bmu_rint_result_t result = compute_result(v1, i1, v2, v3, now_ms);
        if (result.valid) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            s_cache[battery_idx] = result;
            xSemaphoreGive(s_mutex);

            ESP_LOGI(TAG, "BAT[%d] R_ohm=%.1fmΩ R_tot=%.1fmΩ",
                     battery_idx + 1, result.r_ohmic_mohm, result.r_total_mohm);
        } else {
            ESP_LOGW(TAG, "BAT[%d] measurement invalid (V1=%.0f V2=%.0f V3=%.0f I1=%.3f)",
                     battery_idx + 1, v1, v2, v3, i1);
        }

        rint_output_route(battery_idx, trigger, &result);
        ret = result.valid ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
    }

cleanup:
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_measuring = false;
    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t bmu_rint_measure_all(bmu_rint_trigger_t trigger)
{
    int measured = 0;
    for (int i = 0; i < g_nb_ina; i++) {
        if (bmu_protection_get_state(g_prot, i) == BMU_STATE_CONNECTED &&
            count_connected() >= 2)
        {
            esp_err_t ret = bmu_rint_measure(i, trigger);
            if (ret == ESP_OK) measured++;
            // Pause entre batteries pour laisser le bus se stabiliser
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    ESP_LOGI(TAG, "measure_all done: %d/%d measured", measured, g_nb_ina);
    return measured > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

bmu_rint_result_t bmu_rint_get_cached(uint8_t battery_idx)
{
    bmu_rint_result_t r = {};
    if (battery_idx >= BMU_MAX_BATTERIES) return r;
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        r = s_cache[battery_idx];
        xSemaphoreGive(s_mutex);
    }
    return r;
}

void bmu_rint_on_disconnect(uint8_t battery_idx, float v_before_mv, float i_before_a)
{
    if (battery_idx >= g_nb_ina) return;
    if (fabsf(i_before_a) < CONFIG_BMU_RINT_MIN_CURRENT_MA / 1000.0f) return;

    // La batterie vient d'être coupée par la protection.
    // On lit V₂ et V₃ sans toucher au MOSFET (déjà OFF).
    int64_t now_ms = esp_timer_get_time() / 1000;

    vTaskDelay(pdMS_TO_TICKS(CONFIG_BMU_RINT_PULSE_FAST_MS));
    float v2 = 0, i2 = 0;
    if (bmu_ina237_read_voltage_current(&g_ina[battery_idx], &v2, &i2) != ESP_OK) return;

    int remaining = CONFIG_BMU_RINT_PULSE_TOTAL_MS - CONFIG_BMU_RINT_PULSE_FAST_MS;
    if (remaining > 0) vTaskDelay(pdMS_TO_TICKS(remaining));
    float v3 = 0, i3 = 0;
    if (bmu_ina237_read_voltage_current(&g_ina[battery_idx], &v3, &i3) != ESP_OK) return;

    bmu_rint_result_t result = compute_result(v_before_mv, i_before_a, v2, v3, now_ms);
    if (result.valid) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_cache[battery_idx] = result;
        xSemaphoreGive(s_mutex);
        ESP_LOGI(TAG, "BAT[%d] opportunistic R_ohm=%.1fmΩ R_tot=%.1fmΩ",
                 battery_idx + 1, result.r_ohmic_mohm, result.r_total_mohm);
    }

    rint_output_route(battery_idx, BMU_RINT_TRIGGER_OPPORTUNISTIC, &result);
}

// ── Periodic task ─────────────────────────────────────────────────────

static void periodic_task(void *arg)
{
    uint32_t period_ms = CONFIG_BMU_RINT_PERIOD_MIN * 60 * 1000;
    // Attendre 2 minutes au boot pour laisser le système se stabiliser
    vTaskDelay(pdMS_TO_TICKS(120000));

    for (;;) {
        ESP_LOGI(TAG, "Periodic R_int measurement starting");
        bmu_rint_measure_all(BMU_RINT_TRIGGER_PERIODIC);
        vTaskDelay(pdMS_TO_TICKS(period_ms));
    }
}

esp_err_t bmu_rint_start_periodic(void)
{
#if CONFIG_BMU_RINT_PERIODIC_ENABLED
    if (s_periodic_task) return ESP_ERR_INVALID_STATE;
    BaseType_t ret = xTaskCreate(periodic_task, "rint_periodic", 4096, NULL, 2, &s_periodic_task);
    if (ret != pdPASS) return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "Periodic task started (interval=%d min)", CONFIG_BMU_RINT_PERIOD_MIN);
    return ESP_OK;
#else
    ESP_LOGI(TAG, "Periodic measurement disabled");
    return ESP_OK;
#endif
}
```

- [ ] **Step 2: Verify compilation**

Run: `cd firmware-idf && idf.py build 2>&1 | tail -20`

Expected: Build succeeds (with linker warnings about missing `bmu_rint_output.cpp` symbols — that's Task 4)

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_rint/bmu_rint.cpp
git commit -m "feat(rint): core measurement sequence + guards"
```

---

### Task 4: Output routing — MQTT, NVS, formatting

**Files:**
- Create: `firmware-idf/components/bmu_rint/bmu_rint_output.cpp`

- [ ] **Step 1: Write output routing**

Create `firmware-idf/components/bmu_rint/bmu_rint_output.cpp`:
```cpp
#include "bmu_rint.h"
#include "bmu_config.h"

#include <esp_log.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <cstdio>
#include <cstring>

static const char *TAG = "bmu_rint_out";
static const char *NVS_NAMESPACE = "bmu_rint";

// ── InfluxDB formatting ───────────────────────────────────────────────

void rint_format_influx(char *buf, size_t len, int battery_idx,
                         bmu_rint_trigger_t trigger,
                         const bmu_rint_result_t *res)
{
    const char *trig_str = "unknown";
    switch (trigger) {
        case BMU_RINT_TRIGGER_OPPORTUNISTIC: trig_str = "opportunistic"; break;
        case BMU_RINT_TRIGGER_PERIODIC:      trig_str = "periodic";      break;
        case BMU_RINT_TRIGGER_ON_DEMAND:     trig_str = "on_demand";     break;
    }
    float r_polar = res->r_total_mohm - res->r_ohmic_mohm;
    snprintf(buf, len,
             "rint,battery=%d,trigger=%s "
             "r_ohmic_mohm=%.2f,r_total_mohm=%.2f,r_polar_mohm=%.2f,"
             "v_load_mv=%.1f,v_ocv_fast_mv=%.1f,v_ocv_stable_mv=%.1f,"
             "i_load_a=%.3f",
             battery_idx, trig_str,
             res->r_ohmic_mohm, res->r_total_mohm, r_polar,
             res->v_load_mv, res->v_ocv_fast_mv, res->v_ocv_stable_mv,
             res->i_load_a);
}

// ── JSON formatting ───────────────────────────────────────────────────

void rint_format_json(char *buf, size_t len, int battery_idx,
                       const bmu_rint_result_t *res)
{
    float r_polar = res->r_total_mohm - res->r_ohmic_mohm;
    snprintf(buf, len,
             "{\"index\":%d,\"r_ohmic_mohm\":%.2f,\"r_total_mohm\":%.2f,"
             "\"r_polar_mohm\":%.2f,\"v_load_mv\":%.1f,"
             "\"v_ocv_stable_mv\":%.1f,\"i_load_a\":%.3f,"
             "\"timestamp\":%lld,\"valid\":%s}",
             battery_idx, res->r_ohmic_mohm, res->r_total_mohm, r_polar,
             res->v_load_mv, res->v_ocv_stable_mv, res->i_load_a,
             (long long)res->timestamp_ms,
             res->valid ? "true" : "false");
}

// ── NVS persistence ───────────────────────────────────────────────────

static void nvs_save_result(uint8_t idx, const bmu_rint_result_t *res)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs) != ESP_OK) return;

    char key[16];
    snprintf(key, sizeof(key), "rint_%d", idx);
    nvs_set_blob(nvs, key, res, sizeof(bmu_rint_result_t));
    nvs_commit(nvs);
    nvs_close(nvs);
}

void bmu_rint_nvs_load_cache(bmu_rint_result_t *cache, uint8_t nb)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) return;

    for (int i = 0; i < nb && i < BMU_MAX_BATTERIES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "rint_%d", i);
        size_t len = sizeof(bmu_rint_result_t);
        if (nvs_get_blob(nvs, key, &cache[i], &len) == ESP_OK) {
            ESP_LOGD(TAG, "NVS loaded rint_%d: R_ohm=%.1f", i, cache[i].r_ohmic_mohm);
        }
    }
    nvs_close(nvs);
}

// ── MQTT publish (uses existing bmu_mqtt infrastructure) ──────────────

extern esp_err_t bmu_mqtt_publish(const char *topic, const char *data, int len, int qos);

static void mqtt_publish_rint(uint8_t idx, bmu_rint_trigger_t trigger,
                               const bmu_rint_result_t *res)
{
    char topic[64];
    snprintf(topic, sizeof(topic), "bmu/rint/%d", idx);

    char payload[320];
    rint_format_influx(payload, sizeof(payload), idx, trigger, res);

    bmu_mqtt_publish(topic, payload, strlen(payload), 0);
}

// ── Output routing ────────────────────────────────────────────────────

void rint_output_route(uint8_t idx, bmu_rint_trigger_t trigger,
                        const bmu_rint_result_t *res)
{
    if (!res->valid) return;

    // Toujours: MQTT
    mqtt_publish_rint(idx, trigger, res);

    // Periodic + On-demand: NVS persist
    if (trigger == BMU_RINT_TRIGGER_PERIODIC ||
        trigger == BMU_RINT_TRIGGER_ON_DEMAND)
    {
        nvs_save_result(idx, res);
    }

    // On-demand: display update is handled by UI polling bmu_rint_get_cached()
    // On-demand: web API returns results directly from cache
}
```

- [ ] **Step 2: Build full project**

Run: `cd firmware-idf && idf.py build 2>&1 | tail -10`

Expected: Build succeeds. Linker may warn about `bmu_mqtt_publish` if MQTT is not linked — this is resolved when `main.cpp` wires everything together.

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_rint/bmu_rint_output.cpp
git commit -m "feat(rint): output routing — MQTT, NVS, format"
```

---

### Task 5: Opportunistic hook in bmu_protection

**Files:**
- Modify: `firmware-idf/components/bmu_protection/bmu_protection.cpp`

- [ ] **Step 1: Add the hook call at disconnect**

In `bmu_protection.cpp`, add the include at top:
```cpp
#include "bmu_rint.h"
```

In the `BMU_STATE_DISCONNECTED` case (around line 200-203), the current code is:
```cpp
case BMU_STATE_DISCONNECTED:
    ESP_LOGI(TAG, "BAT[%d] disconnected V=%.0fmV I=%.3fA", idx + 1, v_mv, i_a);
    switch_battery(ctx, idx, false);
    break;
```

Change to:
```cpp
case BMU_STATE_DISCONNECTED:
    ESP_LOGI(TAG, "BAT[%d] disconnected V=%.0fmV I=%.3fA", idx + 1, v_mv, i_a);
    switch_battery(ctx, idx, false);
#if CONFIG_BMU_RINT_ENABLED
    bmu_rint_on_disconnect(idx, v_mv, i_a);
#endif
    break;
```

- [ ] **Step 2: Add bmu_rint dependency to protection CMakeLists**

In `firmware-idf/components/bmu_protection/CMakeLists.txt`, add `bmu_rint` to `PRIV_REQUIRES` (or `REQUIRES` if not already present). Check the current file first — only add what's missing.

- [ ] **Step 3: Build and verify**

Run: `cd firmware-idf && idf.py build 2>&1 | tail -10`

Expected: Clean build

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_protection/
git commit -m "feat(rint): opportunistic hook on disconnect"
```

---

### Task 6: Web API endpoints

**Files:**
- Modify: `firmware-idf/components/bmu_web/bmu_web.cpp`

- [ ] **Step 1: Add R_int API handlers**

Add include at top of `bmu_web.cpp`:
```cpp
#include "bmu_rint.h"
```

Add these handler functions (before `bmu_web_start`):
```cpp
// ── R_int API ─────────────────────────────────────────────────────────

static esp_err_t handler_rint_results(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    char buf[4096];
    int pos = snprintf(buf, sizeof(buf), "{\"batteries\":[");

    for (int i = 0; i < s_ctx->nb_ina && i < BMU_MAX_BATTERIES; i++) {
        bmu_rint_result_t r = bmu_rint_get_cached(i);
        if (i > 0) pos += snprintf(buf + pos, sizeof(buf) - pos, ",");

        char entry[320];
        float r_polar = r.r_total_mohm - r.r_ohmic_mohm;
        snprintf(entry, sizeof(entry),
                 "{\"index\":%d,\"r_ohmic_mohm\":%.2f,\"r_total_mohm\":%.2f,"
                 "\"r_polar_mohm\":%.2f,\"v_load_mv\":%.1f,"
                 "\"v_ocv_stable_mv\":%.1f,\"i_load_a\":%.3f,"
                 "\"timestamp\":%lld,\"valid\":%s}",
                 i, r.r_ohmic_mohm, r.r_total_mohm, r_polar,
                 r.v_load_mv, r.v_ocv_stable_mv, r.i_load_a,
                 (long long)r.timestamp_ms,
                 r.valid ? "true" : "false");
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s", entry);
    }

    pos += snprintf(buf + pos, sizeof(buf) - pos, "]}");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t handler_rint_measure(httpd_req_t *req)
{
    // Auth check (same pattern as switch_on/off)
    if (!bmu_web_token_enabled()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "{\"error\":\"mutations disabled\"}");
        return ESP_OK;
    }

    uint32_t ip = get_client_ip(req);
    if (bmu_web_rate_check(ip, now_ms())) {
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_sendstr(req, "{\"error\":\"rate limit exceeded\"}");
        return ESP_OK;
    }

    char token[128] = {};
    char body[256] = {};
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (len > 0) {
        body[len] = '\0';
        // Parse token from JSON body: {"token":"...","battery":-1}
        // -1 or absent = measure all
    }

    // Validate token
    if (!bmu_web_token_valid(token)) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "{\"error\":\"invalid token\"}");
        return ESP_OK;
    }

    // Mesure (bloquant ~1.2s par batterie)
    esp_err_t ret = bmu_rint_measure_all(BMU_RINT_TRIGGER_ON_DEMAND);

    httpd_resp_set_type(req, "application/json");
    if (ret == ESP_OK) {
        // Répondre avec les résultats à jour
        return handler_rint_results(req);
    } else {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "{\"error\":\"no eligible batteries\"}");
        return ESP_OK;
    }
}
```

- [ ] **Step 2: Register routes in bmu_web_start**

In the route registration section of `bmu_web_start()`, add:
```cpp
#if CONFIG_BMU_RINT_ENABLED
    const httpd_uri_t uri_rint_results = {
        .uri      = "/api/rint/results",
        .method   = HTTP_GET,
        .handler  = handler_rint_results,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_rint_results);

    const httpd_uri_t uri_rint_measure = {
        .uri      = "/api/rint/measure",
        .method   = HTTP_POST,
        .handler  = handler_rint_measure,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_rint_measure);
#endif
```

- [ ] **Step 3: Build and verify**

Run: `cd firmware-idf && idf.py build 2>&1 | tail -10`

Expected: Clean build

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_web/bmu_web.cpp
git commit -m "feat(rint): web API — GET results, POST measure"
```

---

### Task 7: BLE characteristics

**Files:**
- Modify: `firmware-idf/components/bmu_ble/bmu_ble_battery_svc.cpp`

- [ ] **Step 1: Add RINT trigger + result characteristics**

Add include:
```cpp
#include "bmu_rint.h"
```

Add packed struct for BLE result payload:
```cpp
typedef struct __attribute__((packed)) {
    uint16_t r_ohmic_mohm_x10;   // R_ohmic * 10 (0.1 mΩ resolution)
    uint16_t r_total_mohm_x10;   // R_total * 10
    uint16_t v_load_mv;          // V sous charge
    uint16_t v_ocv_mv;           // V stable
    int16_t  i_load_ma;          // Courant en mA
    uint8_t  valid;              // 0 or 1
} ble_rint_char_t;
```

Add UUID declarations (following existing pattern with 0x38 and 0x39):
```cpp
static ble_uuid128_t s_rint_trigger_uuid = BMU_BLE_UUID128_DECLARE(0x38, 0x00);
static ble_uuid128_t s_rint_result_uuid  = BMU_BLE_UUID128_DECLARE(0x39, 0x00);
static uint16_t s_rint_trigger_handle = 0;
static uint16_t s_rint_result_handle  = 0;
```

Add trigger write callback:
```cpp
static int rint_trigger_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t val = 0;
    if (os_mbuf_copydata(ctxt->om, 0, 1, &val) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

#if CONFIG_BMU_RINT_ENABLED
    if (val == 0xFF) {
        bmu_rint_measure_all(BMU_RINT_TRIGGER_ON_DEMAND);
    } else if (val < bmu_ble_get_nb_ina()) {
        bmu_rint_measure(val, BMU_RINT_TRIGGER_ON_DEMAND);
    }
#endif
    return 0;
}
```

Add result read callback:
```cpp
static int rint_result_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    // Retourne les résultats de toutes les batteries en séquence
    uint8_t nb = bmu_ble_get_nb_ina();
    for (int i = 0; i < nb; i++) {
        bmu_rint_result_t r = bmu_rint_get_cached(i);
        ble_rint_char_t payload = {
            .r_ohmic_mohm_x10 = (uint16_t)(r.r_ohmic_mohm * 10),
            .r_total_mohm_x10 = (uint16_t)(r.r_total_mohm * 10),
            .v_load_mv        = (uint16_t)r.v_load_mv,
            .v_ocv_mv         = (uint16_t)r.v_ocv_stable_mv,
            .i_load_ma        = (int16_t)(r.i_load_a * 1000),
            .valid             = r.valid ? 1 : 0,
        };
        os_mbuf_append(ctxt->om, &payload, sizeof(payload));
    }
    return 0;
}
```

Register these characteristics in `bmu_ble_battery_svc_defs()` — add them to the GATT service definition array alongside existing battery characteristics. The exact insertion point depends on how the array is structured. Add before the null terminator:
```cpp
// RINT trigger (write-only)
s_rint_chr_defs[0].uuid       = &s_rint_trigger_uuid.u;
s_rint_chr_defs[0].access_cb  = rint_trigger_access_cb;
s_rint_chr_defs[0].flags      = BLE_GATT_CHR_F_WRITE;
s_rint_chr_defs[0].val_handle = &s_rint_trigger_handle;

// RINT result (read + notify)
s_rint_chr_defs[1].uuid       = &s_rint_result_uuid.u;
s_rint_chr_defs[1].access_cb  = rint_result_access_cb;
s_rint_chr_defs[1].flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
s_rint_chr_defs[1].val_handle = &s_rint_result_handle;
```

- [ ] **Step 2: Build and verify**

Run: `cd firmware-idf && idf.py build 2>&1 | tail -10`

Expected: Clean build

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_ble/bmu_ble_battery_svc.cpp
git commit -m "feat(rint): BLE trigger + result characteristics"
```

---

### Task 8: LVGL debug display

**Files:**
- Modify: `firmware-idf/components/bmu_display/bmu_ui_debug.cpp`

- [ ] **Step 1: Add R_int section to debug screen**

Add include:
```cpp
#include "bmu_rint.h"
```

Add a new section at the end of the debug screen creation function. The existing debug screen shows I2C bus logs. Add an R_int summary panel below:

```cpp
// ── R_int section ─────────────────────────────────────────────────────

static lv_obj_t *s_rint_labels[BMU_MAX_BATTERIES] = {};

void bmu_ui_debug_create_rint_section(lv_obj_t *parent, uint8_t nb_ina)
{
    lv_obj_t *header = lv_label_create(parent);
    lv_label_set_text(header, "Internal Resistance (mohm)");
    lv_obj_set_style_text_font(header, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(header, lv_color_hex(0x4FC3F7), 0);

    for (int i = 0; i < nb_ina && i < BMU_MAX_BATTERIES; i++) {
        s_rint_labels[i] = lv_label_create(parent);
        lv_label_set_text_fmt(s_rint_labels[i], "B%02d: --", i + 1);
        lv_obj_set_style_text_font(s_rint_labels[i], &lv_font_montserrat_12, 0);
    }
}

void bmu_ui_debug_update_rint(uint8_t nb_ina)
{
    for (int i = 0; i < nb_ina && i < BMU_MAX_BATTERIES; i++) {
        if (!s_rint_labels[i]) continue;
        bmu_rint_result_t r = bmu_rint_get_cached(i);
        if (!r.valid) {
            lv_label_set_text_fmt(s_rint_labels[i], "B%02d: --", i + 1);
            lv_obj_set_style_text_color(s_rint_labels[i],
                                         lv_color_hex(0x888888), 0);
            continue;
        }

        lv_label_set_text_fmt(s_rint_labels[i],
                               "B%02d: R0=%.1f  Rt=%.1f",
                               i + 1, r.r_ohmic_mohm, r.r_total_mohm);

        // Couleur selon seuils Kconfig
        lv_color_t color;
        if (r.r_ohmic_mohm > CONFIG_BMU_RINT_DISPLAY_CRIT_MOHM) {
            color = lv_color_hex(0xFF4444);  // rouge
        } else if (r.r_ohmic_mohm > CONFIG_BMU_RINT_DISPLAY_WARN_MOHM) {
            color = lv_color_hex(0xFFA726);  // orange
        } else {
            color = lv_color_hex(0x66BB6A);  // vert
        }
        lv_obj_set_style_text_color(s_rint_labels[i], color, 0);
    }
}
```

- [ ] **Step 2: Call from display update loop**

In the display component's periodic update function (wherever `bmu_ui_debug` is refreshed), add:
```cpp
#if CONFIG_BMU_RINT_ENABLED
    bmu_ui_debug_update_rint(nb_ina);
#endif
```

- [ ] **Step 3: Build and verify**

Run: `cd firmware-idf && idf.py build 2>&1 | tail -10`

Expected: Clean build

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_display/bmu_ui_debug.cpp
git commit -m "feat(rint): LVGL debug display with thresholds"
```

---

### Task 9: SOH integration — replace r_int fallback

**Files:**
- Modify: `firmware-idf/components/bmu_soh/bmu_soh.cpp`

- [ ] **Step 1: Replace the hardcoded r_int with cached value**

Add include:
```cpp
#include "bmu_rint.h"
```

Find the existing r_int calculation (around line 148-149):
```cpp
float r_int = (fabsf(di_dt) > 0.001f) ? fabsf(dv_dt / di_dt) : 0.05f;
```

Replace with:
```cpp
float r_int;
#if CONFIG_BMU_RINT_ENABLED
    bmu_rint_result_t rint_cached = bmu_rint_get_cached(idx);
    if (rint_cached.valid) {
        r_int = rint_cached.r_total_mohm / 1000.0f;  // mΩ → Ω
    } else {
        r_int = (fabsf(di_dt) > 0.001f) ? fabsf(dv_dt / di_dt) : 0.05f;
    }
#else
    r_int = (fabsf(di_dt) > 0.001f) ? fabsf(dv_dt / di_dt) : 0.05f;
#endif
```

- [ ] **Step 2: Build and verify**

Run: `cd firmware-idf && idf.py build 2>&1 | tail -10`

Expected: Clean build

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_soh/bmu_soh.cpp
git commit -m "feat(rint): SOH uses cached R_int measurement"
```

---

### Task 10: Wire into main.cpp + sdkconfig defaults

**Files:**
- Modify: `firmware-idf/main/main.cpp`
- Modify: `firmware-idf/main/CMakeLists.txt`
- Modify: `firmware-idf/sdkconfig.defaults`

- [ ] **Step 1: Add globals and init in main.cpp**

Add include at top:
```cpp
#include "bmu_rint.h"
```

Add global pointers (after the static variable declarations, before `app_main`):
```cpp
// Globals pour bmu_rint (extern linkage)
bmu_protection_ctx_t *g_prot = nullptr;
bmu_ina237_t         *g_ina  = nullptr;
bmu_tca9535_handle_t *g_tca  = nullptr;
uint8_t               g_nb_ina = 0;
uint8_t               g_nb_tca = 0;
```

After `bmu_protection_init` and `bmu_battery_manager_init` (around step 9 in main), add:
```cpp
    // Set globals for bmu_rint
    g_prot   = &prot;
    g_ina    = ina;
    g_tca    = tca;
    g_nb_ina = nb_ina;
    g_nb_tca = nb_tca;

#if CONFIG_BMU_RINT_ENABLED
    bmu_rint_init();
    if (nb_ina > 0) {
        bmu_rint_start_periodic();
    }
#endif
```

- [ ] **Step 2: Add bmu_rint to main's CMakeLists**

In `firmware-idf/main/CMakeLists.txt`, add `bmu_rint` to the `REQUIRES` or `PRIV_REQUIRES` list.

- [ ] **Step 3: Add sdkconfig defaults**

Append to `firmware-idf/sdkconfig.defaults`:
```
# BMU Internal Resistance
CONFIG_BMU_RINT_ENABLED=y
CONFIG_BMU_RINT_PERIODIC_ENABLED=y
CONFIG_BMU_RINT_PERIOD_MIN=30
CONFIG_BMU_RINT_MIN_CURRENT_MA=500
CONFIG_BMU_RINT_PULSE_FAST_MS=100
CONFIG_BMU_RINT_PULSE_TOTAL_MS=1000
CONFIG_BMU_RINT_R_MAX_MOHM=500
CONFIG_BMU_RINT_DISPLAY_WARN_MOHM=50
CONFIG_BMU_RINT_DISPLAY_CRIT_MOHM=100
```

- [ ] **Step 4: Full build**

Run: `cd firmware-idf && idf.py fullclean && idf.py build 2>&1 | tail -20`

Expected: Clean build with bmu_rint linked in

- [ ] **Step 5: Memory budget check**

Run: `scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85`

Or manually: `cd firmware-idf && idf.py size 2>&1 | grep -E "Total|Used|Free"`

Expected: Flash < 85% of 2MB OTA partition, RAM < 75%

- [ ] **Step 6: Commit**

```bash
git add firmware-idf/main/main.cpp firmware-idf/main/CMakeLists.txt firmware-idf/sdkconfig.defaults
git commit -m "feat(rint): wire init + periodic in main.cpp"
```

---

### Task 11: Integration test — build + host tests

**Files:**
- No new files — verify everything builds and tests pass

- [ ] **Step 1: Run host tests**

Run: `cd firmware-idf && idf.py -C test/test_rint build && idf.py -C test/test_rint flash monitor`

Expected: 20 tests PASS

- [ ] **Step 2: Run all existing host tests**

Run: `cd firmware-idf && make -C test test-all` (or run each test suite individually)

Expected: All existing tests still pass — no regressions

- [ ] **Step 3: Full firmware build + size check**

Run: `cd firmware-idf && idf.py build && idf.py size-components 2>&1 | head -30`

Expected: `bmu_rint` component < 8 KB flash, total flash within budget

- [ ] **Step 4: Commit if any fixups were needed**

```bash
git add -A
git commit -m "fix(rint): integration fixups"
```

(Skip if no changes needed)

---

## Spec Coverage Check

| Spec Requirement | Task |
|------------------|------|
| `bmu_rint_result_t` struct | Task 2 (header) |
| Guard checks (min connected, error, mutex) | Task 3 (bmu_rint.cpp) |
| Active measurement sequence (V₁→OFF→V₂→V₃→ON) | Task 3 |
| Abort during measurement | Task 3 (has_error_or_locked checks) |
| Opportunistic hook on disconnect | Task 5 |
| Periodic timer task | Task 3 (periodic_task) + Task 10 (start) |
| On-demand via Web API | Task 6 |
| On-demand via BLE | Task 7 |
| MQTT output (all triggers) | Task 4 (bmu_rint_output.cpp) |
| NVS persistence (periodic + on-demand) | Task 4 |
| LVGL debug display (on-demand) | Task 8 |
| Web API JSON response | Task 6 |
| SOH r_int feature replacement | Task 9 |
| Kconfig parameters | Task 2 |
| Host unit tests | Task 1 |
| Integration in main.cpp | Task 10 |
| EIS roadmap (documentation only) | Already in spec appendix — no code needed |
