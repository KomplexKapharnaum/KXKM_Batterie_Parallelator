# ESP-IDF Migration Phase 2: Protection Logic + Battery Manager

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port the battery protection state machine (F01-F08) and energy accounting (Ah tracking) from Arduino to ESP-IDF, using the Phase 0+1 drivers (`bmu_ina237`, `bmu_tca9535`), with all Arduino dependencies (`String`, `millis()`, `KxLogger`) replaced by ESP-IDF equivalents (`esp_log`, `esp_timer`, `snprintf`).

**Architecture:** Two new components: `bmu_protection` (state machine, thresholds, switching) and `bmu_config` (centralized thresholds via Kconfig). `bmu_battery_manager` is folded into `bmu_protection` as it's tightly coupled. The protection logic is a direct port of `BatteryParallelator.cpp` with audit fixes (CRIT-A/B, HIGH-1, MED-1) already applied. FreeRTOS mutex for shared state, `esp_timer_get_time()` replaces `millis()`.

**Tech Stack:** ESP-IDF v5.3, C++, FreeRTOS, Unity tests

**Depends on:** Phase 0+1 complete (`bmu_i2c`, `bmu_ina237`, `bmu_tca9535` on branch `feat/esp-idf-phase0-1`)

---

## File Structure

```
firmware-idf/components/
├── bmu_config/
│   ├── CMakeLists.txt
│   ├── Kconfig                        # menuconfig entries for thresholds
│   ├── include/bmu_config.h           # Compile-time defaults + Kconfig bridge
│   └── bmu_config.cpp                 # NVS runtime override (future)
├── bmu_protection/
│   ├── CMakeLists.txt
│   ├── include/bmu_protection.h       # Protection API: init, check_battery, switch
│   ├── include/bmu_battery_manager.h  # Ah tracking, min/max/avg voltage
│   ├── bmu_protection.cpp             # State machine (CONNECTED/DISCONNECTED/RECONNECTING/ERROR/LOCKED)
│   └── bmu_battery_manager.cpp        # Energy accounting FreeRTOS task
firmware-idf/test/
└── test_protection/
    └── test_protection.cpp            # Unity host tests (ported from sim-host)
firmware-idf/main/
└── main.cpp                           # Updated to use bmu_protection loop
```

---

### Task 1: bmu_config Component

**Files:**
- Create: `firmware-idf/components/bmu_config/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_config/Kconfig`
- Create: `firmware-idf/components/bmu_config/include/bmu_config.h`
- Create: `firmware-idf/components/bmu_config/bmu_config.cpp`

- [ ] **Step 1: Create CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "bmu_config.cpp"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
)
```

- [ ] **Step 2: Create Kconfig**

```
menu "BMU Protection Config"

    config BMU_MIN_VOLTAGE_MV
        int "Seuil sous-tension (mV)"
        default 24000
        help
            Tension minimale avant coupure batterie.

    config BMU_MAX_VOLTAGE_MV
        int "Seuil sur-tension (mV)"
        default 30000
        help
            Tension maximale avant coupure batterie.

    config BMU_MAX_CURRENT_MA
        int "Seuil sur-courant (mA)"
        default 10000
        help
            Courant max absolu avant coupure (10A = 10000mA).

    config BMU_VOLTAGE_DIFF_MV
        int "Tolérance déséquilibre (mV)"
        default 1000
        help
            Différence maximale entre batterie et max flotte (1V = 1000mV).

    config BMU_RECONNECT_DELAY_MS
        int "Délai reconnexion (ms)"
        default 10000
        help
            Délai avant tentative de reconnexion à nb_switch == max.

    config BMU_NB_SWITCH_MAX
        int "Coupures max avant verrouillage"
        default 5

    config BMU_OVERCURRENT_FACTOR
        int "Facteur surcourant critique (x1000)"
        default 2000
        help
            Facteur multiplicatif pour seuil ERROR (2.0 = 2000).
            ERROR si |I| > (factor/1000) * max_current.

    config BMU_LOOP_PERIOD_MS
        int "Période boucle protection (ms)"
        default 500

endmenu
```

- [ ] **Step 3: Create bmu_config.h**

```cpp
#pragma once

#include "sdkconfig.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Seuils protection — valeurs Kconfig (modifiables via idf.py menuconfig) */
#define BMU_MIN_VOLTAGE_MV      CONFIG_BMU_MIN_VOLTAGE_MV
#define BMU_MAX_VOLTAGE_MV      CONFIG_BMU_MAX_VOLTAGE_MV
#define BMU_MAX_CURRENT_MA      CONFIG_BMU_MAX_CURRENT_MA
#define BMU_VOLTAGE_DIFF_MV     CONFIG_BMU_VOLTAGE_DIFF_MV
#define BMU_RECONNECT_DELAY_MS  CONFIG_BMU_RECONNECT_DELAY_MS
#define BMU_NB_SWITCH_MAX       CONFIG_BMU_NB_SWITCH_MAX
#define BMU_OVERCURRENT_FACTOR  CONFIG_BMU_OVERCURRENT_FACTOR
#define BMU_LOOP_PERIOD_MS      CONFIG_BMU_LOOP_PERIOD_MS

/* Nombre max de batteries supportées */
#define BMU_MAX_BATTERIES       16
#define BMU_MAX_TCA             8

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Create bmu_config.cpp (minimal — NVS override future)**

```cpp
#include "bmu_config.h"
#include "esp_log.h"

static const char *TAG = "CFG";

// Pour l'instant les valeurs sont compile-time via Kconfig.
// Phase future : override runtime via NVS.
void bmu_config_log(void)
{
    ESP_LOGI(TAG, "Config: V_min=%dmV V_max=%dmV I_max=%dmA",
             BMU_MIN_VOLTAGE_MV, BMU_MAX_VOLTAGE_MV, BMU_MAX_CURRENT_MA);
    ESP_LOGI(TAG, "Config: V_diff=%dmV delay=%dms nb_switch_max=%d",
             BMU_VOLTAGE_DIFF_MV, BMU_RECONNECT_DELAY_MS, BMU_NB_SWITCH_MAX);
}
```

- [ ] **Step 5: Commit**

```bash
git add firmware-idf/components/bmu_config/
git commit -m "feat(idf): add bmu_config component — Kconfig thresholds for protection"
```

---

### Task 2: bmu_protection Component — State Machine

**Files:**
- Create: `firmware-idf/components/bmu_protection/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_protection/include/bmu_protection.h`
- Create: `firmware-idf/components/bmu_protection/bmu_protection.cpp`

This is the core port of `BatteryParallelator.cpp` to ESP-IDF. Key changes:
- `KxLogger` → `ESP_LOGx("BATT", ...)`
- `String` → `snprintf` or direct printf format in ESP_LOG
- `millis()` → `esp_timer_get_time() / 1000` (returns ms)
- `inaHandler.read_volt()` → `bmu_ina237_read_bus_voltage()`
- `tcaHandler.write()` → `bmu_tca9535_switch_battery()` / `bmu_tca9535_set_led()`
- Audit fixes CRIT-A/B, HIGH-1, MED-1 already applied
- `battery_voltages[]` is private, accessed via mutex

- [ ] **Step 1: Create CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "bmu_protection.cpp" "bmu_battery_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_ina237 bmu_tca9535 bmu_config
)
```

- [ ] **Step 2: Create bmu_protection.h**

```cpp
#pragma once

#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "bmu_config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BMU_STATE_CONNECTED,
    BMU_STATE_DISCONNECTED,
    BMU_STATE_RECONNECTING,
    BMU_STATE_ERROR,
    BMU_STATE_LOCKED
} bmu_battery_state_t;

typedef struct {
    /* Hardware handles (set by init, then read-only) */
    bmu_ina237_t       *ina_devices;
    bmu_tca9535_handle_t *tca_devices;
    uint8_t             nb_ina;
    uint8_t             nb_tca;

    /* State arrays (mutex-protected) */
    SemaphoreHandle_t   state_mutex;
    float               battery_voltages[BMU_MAX_BATTERIES];
    int                 nb_switch[BMU_MAX_BATTERIES];
    int64_t             reconnect_time_ms[BMU_MAX_BATTERIES];
    bmu_battery_state_t battery_state[BMU_MAX_BATTERIES];
} bmu_protection_ctx_t;

/**
 * @brief Initialize the protection context.
 */
esp_err_t bmu_protection_init(bmu_protection_ctx_t *ctx,
                               bmu_ina237_t *ina, uint8_t nb_ina,
                               bmu_tca9535_handle_t *tca, uint8_t nb_tca);

/**
 * @brief Run one protection check cycle for a single battery.
 * This is the core state machine (F01-F08).
 */
esp_err_t bmu_protection_check_battery(bmu_protection_ctx_t *ctx, int battery_idx);

/**
 * @brief Force all batteries OFF (fail-safe).
 */
esp_err_t bmu_protection_all_off(bmu_protection_ctx_t *ctx);

/**
 * @brief Reset switch counter for a battery (after reboot or manual reset).
 */
esp_err_t bmu_protection_reset_switch_count(bmu_protection_ctx_t *ctx, int battery_idx);

/**
 * @brief Get current state of a battery (thread-safe read).
 */
bmu_battery_state_t bmu_protection_get_state(bmu_protection_ctx_t *ctx, int battery_idx);

/**
 * @brief Get cached voltage for a battery (thread-safe read).
 */
float bmu_protection_get_voltage(bmu_protection_ctx_t *ctx, int battery_idx);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Create bmu_protection.cpp**

Port the full state machine from `BatteryParallelator.cpp`. The implementer agent will receive the complete Arduino source and produce the ESP-IDF equivalent. Key logic to preserve exactly:

1. **Read V/I** from INA237 (return on NAN/error)
2. **Determine state:**
   - `ERROR` if V<0 or |I| > 2x max current
   - `DISCONNECTED` if V<1V
   - `LOCKED` if nb_switch > nb_switch_max (permanent, return early)
   - `DISCONNECTED` if !voltage_in_range || !current_in_range || !difference_acceptable || !voltage_offset_ok
   - `RECONNECTING` if nb_switch==0, or (nb_switch < max && delay elapsed), or (nb_switch == max && delay elapsed)
   - `CONNECTED` otherwise
3. **Act on state:**
   - CONNECTED: log only
   - RECONNECTING: check V/I ranges → switch ON + increment nb_switch + record time
   - DISCONNECTED: switch OFF
   - ERROR: switch OFF (double-off for safety)
4. **Threshold checks** (all in mV/mA internally):
   - `is_voltage_within_range`: V*1000 vs min_mV/max_mV
   - `is_current_within_range`: |I| vs max_mA/1000
   - `is_difference_acceptable`: fleet_max_V - V vs diff_threshold_V (mV→V conversion)
5. **Switching** via `bmu_tca9535_switch_battery()` + `bmu_tca9535_set_led()`
6. **Timing** via `esp_timer_get_time() / 1000` (microseconds → milliseconds)

- [ ] **Step 4: Run tests (if host test available)**

- [ ] **Step 5: Commit**

```bash
git add firmware-idf/components/bmu_protection/
git commit -m "feat(idf): add bmu_protection — battery state machine F01-F08 (ESP-IDF port)"
```

---

### Task 3: bmu_battery_manager — Energy Accounting

**Files:**
- Create: `firmware-idf/components/bmu_protection/include/bmu_battery_manager.h`
- Create: `firmware-idf/components/bmu_protection/bmu_battery_manager.cpp`

Port of `BatteryManager.cpp`. Key functions:
- `getMaxVoltage()` / `getMinVoltage()` / `getAverageVoltage()`
- `startAmpereHourTask()` — FreeRTOS task per battery for Ah integration
- `getAmpereHourConsumption()` / `getAmpereHourCharge()` / `getTotalCurrent()`

Changes from Arduino:
- `KxLogger` → `ESP_LOGx("BMGR", ...)`
- `String` → `snprintf`
- Task stack size configurable via Kconfig
- INA reads via `bmu_ina237_read_current()`

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
    float               ah_consumption[BMU_MAX_BATTERIES]; /* Discharge (positive) */
    float               ah_charge[BMU_MAX_BATTERIES];      /* Charge (negative)    */
    TaskHandle_t        ah_tasks[BMU_MAX_BATTERIES];
    bool                ah_task_running[BMU_MAX_BATTERIES];
} bmu_battery_manager_t;

esp_err_t bmu_battery_manager_init(bmu_battery_manager_t *mgr,
                                    bmu_ina237_t *ina, uint8_t nb_ina);

float bmu_battery_manager_get_max_voltage(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_min_voltage(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_avg_voltage(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_ah_consumption(bmu_battery_manager_t *mgr, int idx);
float bmu_battery_manager_get_ah_charge(bmu_battery_manager_t *mgr, int idx);
float bmu_battery_manager_get_total_current(bmu_battery_manager_t *mgr);

esp_err_t bmu_battery_manager_start_ah_task(bmu_battery_manager_t *mgr, int idx);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Create bmu_battery_manager.cpp**

Port energy accounting with ESP-IDF primitives. FreeRTOS task reads current periodically and integrates Ah.

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_protection/include/bmu_battery_manager.h
git add firmware-idf/components/bmu_protection/bmu_battery_manager.cpp
git commit -m "feat(idf): add bmu_battery_manager — Ah tracking via FreeRTOS tasks"
```

---

### Task 4: Update main.cpp — Full Protection Loop

**Files:**
- Modify: `firmware-idf/main/main.cpp`
- Modify: `firmware-idf/main/CMakeLists.txt`

- [ ] **Step 1: Update CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES bmu_i2c bmu_ina237 bmu_tca9535 bmu_protection bmu_config
)
```

- [ ] **Step 2: Update main.cpp**

Replace the Phase 0+1 simple read loop with the full protection state machine:

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
    ESP_LOGI(TAG, "KXKM BMU — ESP-IDF v5.3 (Phase 2)");
    bmu_config_log();

    // I2C init
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(bmu_i2c_init(&i2c_bus));
    bmu_i2c_scan(i2c_bus);

    // INA237 + TCA9535
    static bmu_ina237_t ina[BMU_MAX_BATTERIES] = {};
    uint8_t nb_ina = 0;
    bmu_ina237_scan_init(i2c_bus, 2000, 10.0f, ina, &nb_ina);
    for (int i = 0; i < nb_ina; i++) {
        bmu_ina237_set_bus_voltage_alerts(&ina[i], BMU_MAX_VOLTAGE_MV, BMU_MIN_VOLTAGE_MV);
    }

    static bmu_tca9535_handle_t tca[BMU_MAX_TCA] = {};
    uint8_t nb_tca = 0;
    bmu_tca9535_scan_init(i2c_bus, tca, BMU_MAX_TCA, &nb_tca);

    // Topology validation
    bool topology_valid = (nb_ina > 0) && (nb_tca > 0) && (nb_tca * 4 == nb_ina);
    if (!topology_valid) {
        ESP_LOGE(TAG, "TOPOLOGY MISMATCH — FAIL-SAFE");
    }

    // Protection init
    static bmu_protection_ctx_t prot = {};
    ESP_ERROR_CHECK(bmu_protection_init(&prot, ina, nb_ina, tca, nb_tca));

    // Battery manager init
    static bmu_battery_manager_t mgr = {};
    bmu_battery_manager_init(&mgr, ina, nb_ina);

    // Start Ah tasks for all batteries
    for (int i = 0; i < nb_ina; i++) {
        bmu_battery_manager_start_ah_task(&mgr, i);
    }

    // Main protection loop
    while (true) {
        if (!topology_valid) {
            bmu_protection_all_off(&prot);
            vTaskDelay(pdMS_TO_TICKS(BMU_LOOP_PERIOD_MS));
            continue;
        }

        for (int i = 0; i < nb_ina; i++) {
            bmu_protection_check_battery(&prot, i);
        }

        vTaskDelay(pdMS_TO_TICKS(BMU_LOOP_PERIOD_MS));
    }
}
```

- [ ] **Step 3: Build**

```bash
idf.py build
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/main/
git commit -m "feat(idf): Phase 2 complete — full protection loop with bmu_protection + bmu_battery_manager"
```

---

### Task 5: Port Unity Tests

**Files:**
- Create: `firmware-idf/test/test_protection/test_protection.cpp`

Port the 13 tests from `firmware/test/test_protection/test_protection.cpp` to work with the new ESP-IDF protection logic. For host testing, use the same stub approach (pure logic tests, no I2C).

- [ ] **Step 1: Create test file with same 13 tests**

Same `should_disconnect()` and `should_permanent_lock()` stubs, aligned with Kconfig defaults:
- `BMU_MIN_VOLTAGE_MV = 24000`
- `BMU_MAX_VOLTAGE_MV = 30000`
- `BMU_MAX_CURRENT_MA = 10000` (10A)
- `BMU_VOLTAGE_DIFF_MV = 1000` (1V)
- `BMU_NB_SWITCH_MAX = 5`

- [ ] **Step 2: Verify tests compile and pass**

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/test/
git commit -m "test(idf): port 13 protection tests to ESP-IDF Phase 2"
```
