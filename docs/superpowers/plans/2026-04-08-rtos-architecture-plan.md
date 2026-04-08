# RTOS Architecture Redesign — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Modernize the BMU firmware from polling/shared-mutex to event-driven FreeRTOS with protection as sole switch authority, immutable snapshots, per-device health scores, and dual-bus 32-battery scalability.

**Architecture:** Protection task (prio 8, 100ms hard) produces immutable snapshots pushed to consumer queues. All other components (balancer, display, cloud, BLE) consume snapshots without mutex. Commands flow back via a dedicated cmd queue. I2C uses priority-inheritance mutex. Abstract interfaces enable host testing.

**Tech Stack:** ESP-IDF 5.4, FreeRTOS, C++17, Unity 2.6.1 (host tests), TCA9535 GPIO expander, INA237 current sensor

**Spec:** `docs/superpowers/specs/2026-04-08-rtos-architecture-design.md`

---

## File Structure

### New files to create

| File | Responsibility |
|------|---------------|
| `components/bmu_types/include/bmu_types.h` | Shared types: `bmu_snapshot_t`, `bmu_cmd_t`, `bmu_device_t`, `bmu_i2c_bus_t`, `bmu_device_health_t`, enums |
| `components/bmu_types/include/bmu_ops.h` | Abstract interfaces: `bmu_sensor_ops_t`, `bmu_switch_ops_t`, `bmu_i2c_ops_t` |
| `components/bmu_types/CMakeLists.txt` | Header-only component |
| `test/test_health_score/main/test_health_score.cpp` | Health score logic tests |
| `test/test_health_score/CMakeLists.txt` | Test project |
| `test/test_health_score/main/CMakeLists.txt` | Test component |
| `test/test_snapshot/main/test_snapshot.cpp` | Snapshot creation/queue tests |
| `test/test_snapshot/CMakeLists.txt` | Test project |
| `test/test_snapshot/main/CMakeLists.txt` | Test component |
| `test/test_balancer_logic/main/test_balancer_logic.cpp` | Balancer request logic tests |
| `test/test_balancer_logic/CMakeLists.txt` | Test project |
| `test/test_balancer_logic/main/CMakeLists.txt` | Test component |

### Files to modify

| File | Nature of change |
|------|-----------------|
| `components/bmu_protection/include/bmu_protection.h` | Add snapshot production, cmd queue, ops injection |
| `components/bmu_protection/bmu_protection.cpp` | Task-based loop, snapshot producer, cmd consumer |
| `components/bmu_balancer/bmu_balancer.cpp` | Task-based, snapshot consumer, BALANCE_REQUEST producer |
| `components/bmu_balancer/include/bmu_balancer.h` | New init signature with queues |
| `components/bmu_i2c/bmu_i2c.cpp` | Priority inheritance mutex, per-device health tracking |
| `components/bmu_i2c/include/bmu_i2c.h` | Health score API, bus abstraction |
| `components/bmu_ina237/include/bmu_ina237.h` | Accept `bmu_device_t*` |
| `components/bmu_ina237/bmu_ina237.cpp` | Use `bmu_device_t`, report health |
| `components/bmu_tca9535/include/bmu_tca9535.h` | Accept `bmu_device_t*` |
| `components/bmu_tca9535/bmu_tca9535.cpp` | Use `bmu_device_t`, report health |
| `components/bmu_i2c_hotplug/bmu_i2c_hotplug.cpp` | Post CMD_TOPOLOGY_CHANGED instead of direct mutation |
| `components/bmu_i2c_hotplug/include/bmu_i2c_hotplug.h` | New config with cmd queue handle |
| `components/bmu_display/bmu_ui_detail.cpp` | Read from snapshot instead of protection ctx |
| `components/bmu_influx/bmu_influx.cpp` | Read from snapshot |
| `main/main.cpp` | Create tasks, queues, wire everything, remove main loop |
| `test/Makefile` | Add new test targets |

All paths relative to `firmware-idf/`.

---

## Phase 1 — Foundations (no behavior change)

### Task 1: Create `bmu_types` component with shared types

**Files:**
- Create: `components/bmu_types/include/bmu_types.h`
- Create: `components/bmu_types/include/bmu_ops.h`
- Create: `components/bmu_types/CMakeLists.txt`

- [ ] **Step 1: Create CMakeLists.txt (header-only component)**

```cmake
# components/bmu_types/CMakeLists.txt
idf_component_register(
    INCLUDE_DIRS "include"
)
```

- [ ] **Step 2: Create `bmu_types.h` with snapshot, device, health, and cmd types**

```cpp
// components/bmu_types/include/bmu_types.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Forward declarations (ESP-IDF) ──
// Avoid including full ESP-IDF headers in host tests
#ifdef NATIVE_TEST
    typedef void* i2c_master_bus_handle_t;
    typedef void* i2c_master_dev_handle_t;
    typedef void* SemaphoreHandle_t;
    typedef void* QueueHandle_t;
    typedef int esp_err_t;
    #define ESP_OK 0
    #define ESP_FAIL -1
    #define ESP_ERR_TIMEOUT 0x107
#else
    #include "driver/i2c_master.h"
    #include "freertos/FreeRTOS.h"
    #include "freertos/semphr.h"
    #include "freertos/queue.h"
#endif

// ── Constants ──
#define BMU_MAX_BATTERIES       32
#define BMU_MAX_BUSES           2
#define BMU_HEALTH_SCORE_MAX    100
#define BMU_HEALTH_SCORE_INIT   100
#define BMU_HEALTH_OK_INCR      5
#define BMU_HEALTH_FAIL_DECR    20
#define BMU_HEALTH_THRESH_WARN  60
#define BMU_HEALTH_THRESH_CRIT  30
#define BMU_HEALTH_THRESH_RECONNECT 60

// ── Battery state (unchanged enum values) ──
typedef enum {
    BMU_STATE_CONNECTED = 0,
    BMU_STATE_DISCONNECTED,
    BMU_STATE_RECONNECTING,
    BMU_STATE_ERROR,
    BMU_STATE_LOCKED
} bmu_battery_state_t;

// ── Device health ──
typedef struct {
    uint8_t score;          // 0-100
    uint8_t consec_fails;   // for bus recovery quorum
} bmu_device_health_t;

// ── I2C bus abstraction ──
typedef struct {
    i2c_master_bus_handle_t handle;
    SemaphoreHandle_t       mutex;
    uint8_t                 bus_id;     // 0=DOCK hw, 1=bitbang
    bmu_device_health_t     bus_health;
} bmu_i2c_bus_t;

// ── Device abstraction (one per INA237 or TCA9535) ──
typedef struct {
    uint8_t                 bus_id;
    uint8_t                 local_idx;  // 0-15 on this bus
    uint8_t                 global_idx; // 0-31
    uint8_t                 addr;       // I2C address
    i2c_master_dev_handle_t handle;
    bmu_device_health_t     health;
} bmu_device_t;

// ── Immutable snapshot (produced by protection each cycle) ──
typedef struct {
    uint32_t timestamp_ms;
    uint16_t cycle_count;
    uint8_t  nb_batteries;
    bool     topology_ok;

    struct {
        float               voltage_mv;
        float               current_a;
        bmu_battery_state_t state;
        uint8_t             health_score;
        uint8_t             nb_switches;
        bool                balancer_active;
    } battery[BMU_MAX_BATTERIES];

    float fleet_max_mv;
    float fleet_mean_mv;
} bmu_snapshot_t;

// ── Command types (cmd queue → protection) ──
typedef enum {
    CMD_TOPOLOGY_CHANGED,
    CMD_BALANCE_REQUEST,
    CMD_WEB_SWITCH,
    CMD_CONFIG_UPDATE,
    CMD_BUS_RECOVERY,
} bmu_cmd_type_t;

typedef struct {
    bmu_cmd_type_t type;
    union {
        struct {
            uint8_t nb_ina;
            uint8_t nb_tca;
        } topology;
        struct {
            uint8_t battery_idx;
            bool    on;
        } balance_req;
        struct {
            uint8_t battery_idx;
            bool    on;
        } web_switch;
        struct {
            uint8_t bus_id;
        } bus_recovery;
    } payload;
} bmu_cmd_t;

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Create `bmu_ops.h` with abstract interfaces**

```cpp
// components/bmu_types/include/bmu_ops.h
#pragma once

#include "bmu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── I2C operations (real or mock) ──
typedef struct {
    esp_err_t (*read_register)(bmu_device_t *dev, uint8_t reg, uint8_t *buf, size_t len);
    esp_err_t (*write_register)(bmu_device_t *dev, uint8_t reg, const uint8_t *buf, size_t len);
    esp_err_t (*probe)(bmu_i2c_bus_t *bus, uint8_t addr);
} bmu_i2c_ops_t;

// ── Sensor operations (INA237 real or mock) ──
typedef struct {
    esp_err_t (*read_voltage_current)(bmu_device_t *dev, float *voltage_mv, float *current_a);
    void      (*record_health)(bmu_device_t *dev, bool success);
} bmu_sensor_ops_t;

// ── Switch operations (TCA9535 real or mock) ──
typedef struct {
    esp_err_t (*switch_battery)(bmu_device_t *dev, uint8_t channel, bool on);
    esp_err_t (*set_led)(bmu_device_t *dev, uint8_t channel, bool red, bool green);
    esp_err_t (*all_off)(bmu_device_t *dev);
} bmu_switch_ops_t;

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Verify the component compiles**

Run:
```bash
cd firmware-idf && idf.py build
```
Expected: Build succeeds (bmu_types is header-only, no consumers yet).

- [ ] **Step 5: Commit**

```bash
git add components/bmu_types/
git commit -m "feat: add bmu_types component with snapshot, device, and ops interfaces"
```

---

### Task 2: Health score logic + host tests

**Files:**
- Create: `test/test_health_score/CMakeLists.txt`
- Create: `test/test_health_score/main/CMakeLists.txt`
- Create: `test/test_health_score/main/test_health_score.cpp`
- Modify: `test/Makefile`

- [ ] **Step 1: Write failing health score tests**

```cpp
// test/test_health_score/main/test_health_score.cpp
#define NATIVE_TEST
#include <unity.h>
#include "bmu_types.h"

// ── Health score helper (pure logic, will move to bmu_types later if needed) ──
static void health_record_success(bmu_device_health_t *h) {
    if (h->score <= BMU_HEALTH_SCORE_MAX - BMU_HEALTH_OK_INCR)
        h->score += BMU_HEALTH_OK_INCR;
    else
        h->score = BMU_HEALTH_SCORE_MAX;
    h->consec_fails = 0;
}

static void health_record_failure(bmu_device_health_t *h) {
    if (h->score >= BMU_HEALTH_FAIL_DECR)
        h->score -= BMU_HEALTH_FAIL_DECR;
    else
        h->score = 0;
    h->consec_fails++;
}

void setUp(void) {}
void tearDown(void) {}

void test_health_init_at_max(void) {
    bmu_device_health_t h = { .score = BMU_HEALTH_SCORE_INIT, .consec_fails = 0 };
    TEST_ASSERT_EQUAL_UINT8(100, h.score);
    TEST_ASSERT_EQUAL_UINT8(0, h.consec_fails);
}

void test_health_success_increments(void) {
    bmu_device_health_t h = { .score = 50, .consec_fails = 3 };
    health_record_success(&h);
    TEST_ASSERT_EQUAL_UINT8(55, h.score);
    TEST_ASSERT_EQUAL_UINT8(0, h.consec_fails);
}

void test_health_success_caps_at_100(void) {
    bmu_device_health_t h = { .score = 98, .consec_fails = 0 };
    health_record_success(&h);
    TEST_ASSERT_EQUAL_UINT8(100, h.score);
}

void test_health_failure_decrements(void) {
    bmu_device_health_t h = { .score = 100, .consec_fails = 0 };
    health_record_failure(&h);
    TEST_ASSERT_EQUAL_UINT8(80, h.score);
    TEST_ASSERT_EQUAL_UINT8(1, h.consec_fails);
}

void test_health_failure_floors_at_zero(void) {
    bmu_device_health_t h = { .score = 10, .consec_fails = 0 };
    health_record_failure(&h);
    TEST_ASSERT_EQUAL_UINT8(0, h.score);
}

void test_health_five_failures_reaches_zero(void) {
    bmu_device_health_t h = { .score = 100, .consec_fails = 0 };
    for (int i = 0; i < 5; i++) health_record_failure(&h);
    TEST_ASSERT_EQUAL_UINT8(0, h.score);
    TEST_ASSERT_EQUAL_UINT8(5, h.consec_fails);
}

void test_health_twenty_successes_full_recovery(void) {
    bmu_device_health_t h = { .score = 0, .consec_fails = 5 };
    for (int i = 0; i < 20; i++) health_record_success(&h);
    TEST_ASSERT_EQUAL_UINT8(100, h.score);
}

void test_health_warn_threshold(void) {
    bmu_device_health_t h = { .score = 60, .consec_fails = 0 };
    TEST_ASSERT_TRUE(h.score >= BMU_HEALTH_THRESH_WARN);
    health_record_failure(&h);
    TEST_ASSERT_TRUE(h.score < BMU_HEALTH_THRESH_WARN);
}

void test_health_critical_threshold(void) {
    bmu_device_health_t h = { .score = 40, .consec_fails = 0 };
    health_record_failure(&h);  // 40 → 20
    TEST_ASSERT_TRUE(h.score < BMU_HEALTH_THRESH_CRIT);
}

void test_health_reconnect_hysteresis(void) {
    // Battery forced OFF at score < 30
    // Must reach 60 to reconnect
    bmu_device_health_t h = { .score = 25, .consec_fails = 0 };
    TEST_ASSERT_TRUE(h.score < BMU_HEALTH_THRESH_CRIT);

    // 7 successes: 25 + 7*5 = 60 → exactly at reconnect threshold
    for (int i = 0; i < 7; i++) health_record_success(&h);
    TEST_ASSERT_TRUE(h.score >= BMU_HEALTH_THRESH_RECONNECT);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_health_init_at_max);
    RUN_TEST(test_health_success_increments);
    RUN_TEST(test_health_success_caps_at_100);
    RUN_TEST(test_health_failure_decrements);
    RUN_TEST(test_health_failure_floors_at_zero);
    RUN_TEST(test_health_five_failures_reaches_zero);
    RUN_TEST(test_health_twenty_successes_full_recovery);
    RUN_TEST(test_health_warn_threshold);
    RUN_TEST(test_health_critical_threshold);
    RUN_TEST(test_health_reconnect_hysteresis);
    return UNITY_END();
}
```

- [ ] **Step 2: Create test CMakeLists files**

```cmake
# test/test_health_score/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_health_score)
```

```cmake
# test/test_health_score/main/CMakeLists.txt
idf_component_register(
    SRCS "test_health_score.cpp"
    INCLUDE_DIRS "."
    REQUIRES unity
)
```

- [ ] **Step 3: Add test_health_score to Makefile**

Add to the existing `TESTS` variable and build rules in `test/Makefile`:

```makefile
# Add to TESTS list:
TESTS += test_health_score

# Add build rule:
build/test_health_score: test_health_score/main/test_health_score.cpp $(UNITY_SRC)
	$(CXX) $(CXXFLAGS) -I../components/bmu_types/include -I$(UNITY_DIR)/src \
		$< $(UNITY_SRC) -o $@
```

- [ ] **Step 4: Run tests, verify they pass**

Run:
```bash
cd firmware-idf/test && make build/test_health_score && ./build/test_health_score
```
Expected: 10 tests, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add test/test_health_score/ test/Makefile
git commit -m "test: health score logic (10 tests)"
```

---

### Task 3: Snapshot creation tests

**Files:**
- Create: `test/test_snapshot/CMakeLists.txt`
- Create: `test/test_snapshot/main/CMakeLists.txt`
- Create: `test/test_snapshot/main/test_snapshot.cpp`
- Modify: `test/Makefile`

- [ ] **Step 1: Write snapshot creation and validation tests**

```cpp
// test/test_snapshot/main/test_snapshot.cpp
#define NATIVE_TEST
#include <unity.h>
#include <string.h>
#include "bmu_types.h"

void setUp(void) {}
void tearDown(void) {}

void test_snapshot_zero_init(void) {
    bmu_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    TEST_ASSERT_EQUAL_UINT8(0, snap.nb_batteries);
    TEST_ASSERT_EQUAL_UINT16(0, snap.cycle_count);
    TEST_ASSERT_FALSE(snap.topology_ok);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, snap.fleet_max_mv);
}

void test_snapshot_populate_single_battery(void) {
    bmu_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.nb_batteries = 1;
    snap.topology_ok = true;
    snap.cycle_count = 42;
    snap.timestamp_ms = 12345;

    snap.battery[0].voltage_mv = 27500.0f;
    snap.battery[0].current_a = 3.5f;
    snap.battery[0].state = BMU_STATE_CONNECTED;
    snap.battery[0].health_score = 100;
    snap.battery[0].nb_switches = 0;
    snap.battery[0].balancer_active = false;

    snap.fleet_max_mv = 27500.0f;
    snap.fleet_mean_mv = 27500.0f;

    TEST_ASSERT_EQUAL_UINT8(1, snap.nb_batteries);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 27500.0f, snap.battery[0].voltage_mv);
    TEST_ASSERT_EQUAL(BMU_STATE_CONNECTED, snap.battery[0].state);
}

void test_snapshot_fleet_max_ignores_disconnected(void) {
    // Simulate fleet_max computation logic
    bmu_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.nb_batteries = 3;

    snap.battery[0].voltage_mv = 27000.0f;
    snap.battery[0].state = BMU_STATE_CONNECTED;
    snap.battery[1].voltage_mv = 28000.0f;
    snap.battery[1].state = BMU_STATE_DISCONNECTED;  // should be ignored
    snap.battery[2].voltage_mv = 27500.0f;
    snap.battery[2].state = BMU_STATE_CONNECTED;

    // Compute fleet_max (only CONNECTED/RECONNECTING)
    float max_v = 0;
    for (int i = 0; i < snap.nb_batteries; i++) {
        if (snap.battery[i].state == BMU_STATE_CONNECTED ||
            snap.battery[i].state == BMU_STATE_RECONNECTING) {
            if (snap.battery[i].voltage_mv > max_v)
                max_v = snap.battery[i].voltage_mv;
        }
    }
    snap.fleet_max_mv = max_v;

    TEST_ASSERT_FLOAT_WITHIN(0.1f, 27500.0f, snap.fleet_max_mv);
    // 28000 (disconnected) was correctly ignored
}

void test_snapshot_max_batteries_fits(void) {
    bmu_snapshot_t snap;
    memset(&snap, 0, sizeof(snap));
    snap.nb_batteries = BMU_MAX_BATTERIES;

    for (int i = 0; i < BMU_MAX_BATTERIES; i++) {
        snap.battery[i].voltage_mv = 27000.0f + (float)i;
        snap.battery[i].state = BMU_STATE_CONNECTED;
        snap.battery[i].health_score = 100;
    }

    TEST_ASSERT_EQUAL_UINT8(32, snap.nb_batteries);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 27031.0f, snap.battery[31].voltage_mv);
}

void test_snapshot_cmd_topology_changed(void) {
    bmu_cmd_t cmd;
    cmd.type = CMD_TOPOLOGY_CHANGED;
    cmd.payload.topology.nb_ina = 16;
    cmd.payload.topology.nb_tca = 4;

    TEST_ASSERT_EQUAL(CMD_TOPOLOGY_CHANGED, cmd.type);
    TEST_ASSERT_EQUAL_UINT8(16, cmd.payload.topology.nb_ina);
    TEST_ASSERT_EQUAL_UINT8(4, cmd.payload.topology.nb_tca);
}

void test_snapshot_cmd_balance_request(void) {
    bmu_cmd_t cmd;
    cmd.type = CMD_BALANCE_REQUEST;
    cmd.payload.balance_req.battery_idx = 7;
    cmd.payload.balance_req.on = false;

    TEST_ASSERT_EQUAL(CMD_BALANCE_REQUEST, cmd.type);
    TEST_ASSERT_EQUAL_UINT8(7, cmd.payload.balance_req.battery_idx);
    TEST_ASSERT_FALSE(cmd.payload.balance_req.on);
}

void test_snapshot_cmd_web_switch(void) {
    bmu_cmd_t cmd;
    cmd.type = CMD_WEB_SWITCH;
    cmd.payload.web_switch.battery_idx = 3;
    cmd.payload.web_switch.on = true;

    TEST_ASSERT_EQUAL(CMD_WEB_SWITCH, cmd.type);
    TEST_ASSERT_TRUE(cmd.payload.web_switch.on);
}

void test_device_global_idx_mapping(void) {
    bmu_device_t dev;
    dev.bus_id = 1;
    dev.local_idx = 5;
    dev.global_idx = 16 + 5;  // bus1 offset

    TEST_ASSERT_EQUAL_UINT8(21, dev.global_idx);
    TEST_ASSERT_EQUAL_UINT8(1, dev.bus_id);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_snapshot_zero_init);
    RUN_TEST(test_snapshot_populate_single_battery);
    RUN_TEST(test_snapshot_fleet_max_ignores_disconnected);
    RUN_TEST(test_snapshot_max_batteries_fits);
    RUN_TEST(test_snapshot_cmd_topology_changed);
    RUN_TEST(test_snapshot_cmd_balance_request);
    RUN_TEST(test_snapshot_cmd_web_switch);
    RUN_TEST(test_device_global_idx_mapping);
    return UNITY_END();
}
```

- [ ] **Step 2: Create test CMakeLists files**

```cmake
# test/test_snapshot/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_snapshot)
```

```cmake
# test/test_snapshot/main/CMakeLists.txt
idf_component_register(
    SRCS "test_snapshot.cpp"
    INCLUDE_DIRS "."
    REQUIRES unity
)
```

- [ ] **Step 3: Add to Makefile**

```makefile
# Add to TESTS list:
TESTS += test_snapshot

# Add build rule:
build/test_snapshot: test_snapshot/main/test_snapshot.cpp $(UNITY_SRC)
	$(CXX) $(CXXFLAGS) -I../components/bmu_types/include -I$(UNITY_DIR)/src \
		$< $(UNITY_SRC) -o $@
```

- [ ] **Step 4: Run tests**

Run:
```bash
cd firmware-idf/test && make build/test_snapshot && ./build/test_snapshot
```
Expected: 8 tests, 0 failures.

- [ ] **Step 5: Commit**

```bash
git add test/test_snapshot/ test/Makefile
git commit -m "test: snapshot and cmd struct validation (8 tests)"
```

---

### Task 4: Upgrade I2C mutex to priority inheritance

**Files:**
- Modify: `components/bmu_i2c/bmu_i2c.cpp`

- [ ] **Step 1: Read current mutex creation**

Read `components/bmu_i2c/bmu_i2c.cpp` lines 14-33 to confirm current `xSemaphoreCreateMutex()` call.

- [ ] **Step 2: Replace with priority inheritance mutex**

`xSemaphoreCreateMutex()` already creates a mutex with priority inheritance in FreeRTOS. Verify this is the case (not `xSemaphoreCreateBinary()`). If it's already `xSemaphoreCreateMutex()`, no code change needed — just add a comment confirming the design intent.

In `bmu_i2c.cpp`, at the mutex creation line:

```cpp
// Replace:
s_i2c_mutex = xSemaphoreCreateMutex();

// With (same function, explicit comment):
// Priority inheritance: if prio-3 task holds this mutex and prio-8 protection
// task blocks on it, FreeRTOS boosts prio-3 to prio-8 until release.
s_i2c_mutex = xSemaphoreCreateMutex();
```

Note: `xSemaphoreCreateMutex()` in FreeRTOS already provides priority inheritance. No API change needed. The comment documents the architectural intent.

- [ ] **Step 3: Build and verify**

Run:
```bash
cd firmware-idf && idf.py build
```
Expected: Build succeeds, no functional change.

- [ ] **Step 4: Commit**

```bash
git add components/bmu_i2c/bmu_i2c.cpp
git commit -m "docs(i2c): document priority inheritance on I2C mutex"
```

---

### Task 5: Add per-device health tracking to I2C layer

**Files:**
- Modify: `components/bmu_i2c/include/bmu_i2c.h`
- Modify: `components/bmu_i2c/bmu_i2c.cpp`
- Modify: `components/bmu_i2c/CMakeLists.txt`

- [ ] **Step 1: Add bmu_types dependency to bmu_i2c CMakeLists**

In `components/bmu_i2c/CMakeLists.txt`, add `bmu_types` to the REQUIRES list.

- [ ] **Step 2: Add health score functions to header**

Append to `components/bmu_i2c/include/bmu_i2c.h`:

```cpp
#include "bmu_types.h"

// Per-device health tracking
void bmu_i2c_health_record_success(bmu_device_health_t *health);
void bmu_i2c_health_record_failure(bmu_device_health_t *health);
bool bmu_i2c_health_is_warn(const bmu_device_health_t *health);
bool bmu_i2c_health_is_critical(const bmu_device_health_t *health);
bool bmu_i2c_health_can_reconnect(const bmu_device_health_t *health);
```

- [ ] **Step 3: Implement health score functions**

Append to `components/bmu_i2c/bmu_i2c.cpp`:

```cpp
void bmu_i2c_health_record_success(bmu_device_health_t *health) {
    if (health->score <= BMU_HEALTH_SCORE_MAX - BMU_HEALTH_OK_INCR)
        health->score += BMU_HEALTH_OK_INCR;
    else
        health->score = BMU_HEALTH_SCORE_MAX;
    health->consec_fails = 0;
}

void bmu_i2c_health_record_failure(bmu_device_health_t *health) {
    if (health->score >= BMU_HEALTH_FAIL_DECR)
        health->score -= BMU_HEALTH_FAIL_DECR;
    else
        health->score = 0;
    health->consec_fails++;
}

bool bmu_i2c_health_is_warn(const bmu_device_health_t *health) {
    return health->score < BMU_HEALTH_THRESH_WARN;
}

bool bmu_i2c_health_is_critical(const bmu_device_health_t *health) {
    return health->score < BMU_HEALTH_THRESH_CRIT;
}

bool bmu_i2c_health_can_reconnect(const bmu_device_health_t *health) {
    return health->score >= BMU_HEALTH_THRESH_RECONNECT;
}
```

- [ ] **Step 4: Build**

Run:
```bash
cd firmware-idf && idf.py build
```
Expected: Build succeeds. Health functions exist but aren't called yet.

- [ ] **Step 5: Verify host tests still pass**

Run:
```bash
cd firmware-idf/test && make run
```
Expected: All existing + new tests pass.

- [ ] **Step 6: Commit**

```bash
git add components/bmu_i2c/ components/bmu_types/
git commit -m "feat(i2c): per-device health score tracking"
```

---

### Task 6: Flash and validate Phase 1

**Files:** None (validation only)

- [ ] **Step 1: Full build**

Run:
```bash
cd firmware-idf && idf.py build
```
Expected: Build succeeds, binary size within budget.

- [ ] **Step 2: Flash to device**

Run:
```bash
cd firmware-idf && idf.py -p /dev/cu.usbmodem* flash monitor
```
Expected: Boot sequence completes, 8 batteries detected, no watchdog, no crash. Behavior identical to before Phase 1.

- [ ] **Step 3: Monitor for 2 minutes**

Watch serial output for:
- All 8 batteries cycling through protection checks
- No I2C errors
- No state machine anomalies
- Display updates normally

- [ ] **Step 4: Commit tag**

```bash
git tag phase1-foundations
```

---

## Phase 2 — Protection as autonomous task

### Task 7: Create snapshot queues and cmd queue in main.cpp

**Files:**
- Modify: `main/main.cpp`
- Modify: `main/CMakeLists.txt`

- [ ] **Step 1: Add bmu_types to main CMakeLists REQUIRES**

In `main/CMakeLists.txt`, add `bmu_types` to the REQUIRES list.

- [ ] **Step 2: Declare queue handles in main.cpp**

After the existing `#include` block and static variables in `main.cpp`, add:

```cpp
#include "bmu_types.h"

// ── Snapshot distribution queues ──
static QueueHandle_t s_q_balancer = NULL;
static QueueHandle_t s_q_display  = NULL;
static QueueHandle_t s_q_cloud    = NULL;
static QueueHandle_t s_q_ble      = NULL;

// ── Command queue → protection ──
static QueueHandle_t s_q_cmd      = NULL;
```

- [ ] **Step 3: Create queues in app_main, after NVS init but before I2C scan**

Insert in app_main after config init:

```cpp
// ── Create RTOS queues ──
s_q_balancer = xQueueCreate(1, sizeof(bmu_snapshot_t));
s_q_display  = xQueueCreate(1, sizeof(bmu_snapshot_t));
s_q_cloud    = xQueueCreate(2, sizeof(bmu_snapshot_t));
s_q_ble      = xQueueCreate(1, sizeof(bmu_snapshot_t));
s_q_cmd      = xQueueCreate(8, sizeof(bmu_cmd_t));

if (!s_q_balancer || !s_q_display || !s_q_cloud || !s_q_ble || !s_q_cmd) {
    ESP_LOGE(TAG, "Failed to create RTOS queues");
    return;
}
ESP_LOGI(TAG, "RTOS queues created (snapshot + cmd)");
```

- [ ] **Step 4: Build**

Run:
```bash
cd firmware-idf && idf.py build
```
Expected: Build succeeds. Queues created but not used yet.

- [ ] **Step 5: Commit**

```bash
git add main/
git commit -m "feat: create snapshot and cmd RTOS queues"
```

---

### Task 8: Refactor protection to produce snapshots

**Files:**
- Modify: `components/bmu_protection/include/bmu_protection.h`
- Modify: `components/bmu_protection/bmu_protection.cpp`
- Modify: `components/bmu_protection/CMakeLists.txt`

- [ ] **Step 1: Add snapshot queue config to protection header**

Add to `bmu_protection.h`, after the existing `bmu_protection_ctx_t`:

```cpp
#include "bmu_types.h"

// ── Queue configuration for snapshot distribution ──
typedef struct {
    QueueHandle_t q_balancer;
    QueueHandle_t q_display;
    QueueHandle_t q_cloud;
    QueueHandle_t q_ble;
    QueueHandle_t q_cmd;
} bmu_protection_queues_t;

// Set queues (called after init, before task start)
esp_err_t bmu_protection_set_queues(bmu_protection_ctx_t *ctx,
                                     const bmu_protection_queues_t *queues);

// Build and distribute snapshot (called internally after each cycle)
void bmu_protection_publish_snapshot(bmu_protection_ctx_t *ctx);
```

- [ ] **Step 2: Add bmu_types to protection CMakeLists REQUIRES**

In `components/bmu_protection/CMakeLists.txt`, add `bmu_types` to REQUIRES.

- [ ] **Step 3: Add queue storage to protection context**

Add to `bmu_protection_ctx_t` in the header:

```cpp
// Add these fields to the existing struct:
bmu_protection_queues_t queues;
uint16_t                cycle_count;
```

- [ ] **Step 4: Implement set_queues and publish_snapshot**

Add to `bmu_protection.cpp`:

```cpp
esp_err_t bmu_protection_set_queues(bmu_protection_ctx_t *ctx,
                                     const bmu_protection_queues_t *queues) {
    if (!ctx || !queues) return ESP_ERR_INVALID_ARG;
    ctx->queues = *queues;
    ctx->cycle_count = 0;
    return ESP_OK;
}

void bmu_protection_publish_snapshot(bmu_protection_ctx_t *ctx) {
    bmu_snapshot_t snap = {};
    snap.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000);
    snap.cycle_count = ctx->cycle_count++;
    snap.nb_batteries = ctx->nb_ina;
    snap.topology_ok = (ctx->nb_tca * 4 == ctx->nb_ina);

    float max_v = 0, sum_v = 0;
    int count_connected = 0;

    for (int i = 0; i < ctx->nb_ina; i++) {
        snap.battery[i].voltage_mv  = ctx->battery_voltages[i];
        snap.battery[i].state       = ctx->battery_state[i];
        snap.battery[i].nb_switches = (uint8_t)ctx->nb_switch[i];
        snap.battery[i].health_score = BMU_HEALTH_SCORE_MAX; // Phase 1: always 100
        snap.battery[i].balancer_active = false;              // Phase 2: not yet

        // Read current inline (already done in check_battery_ex, cache it)
        // For now, current_a is 0 — will be populated in Phase 2 task refactor
        snap.battery[i].current_a = 0;

        if (ctx->battery_state[i] == BMU_STATE_CONNECTED ||
            ctx->battery_state[i] == BMU_STATE_RECONNECTING) {
            if (ctx->battery_voltages[i] > max_v)
                max_v = ctx->battery_voltages[i];
            sum_v += ctx->battery_voltages[i];
            count_connected++;
        }
    }

    snap.fleet_max_mv = max_v;
    snap.fleet_mean_mv = count_connected > 0 ? sum_v / count_connected : 0;

    // Overwrite queues (consumers always want latest)
    if (ctx->queues.q_balancer)
        xQueueOverwrite(ctx->queues.q_balancer, &snap);
    if (ctx->queues.q_display)
        xQueueOverwrite(ctx->queues.q_display, &snap);
    if (ctx->queues.q_cloud)
        xQueueOverwrite(ctx->queues.q_cloud, &snap);
    if (ctx->queues.q_ble)
        xQueueOverwrite(ctx->queues.q_ble, &snap);
}
```

- [ ] **Step 5: Call publish_snapshot from main loop (transitional)**

In `main.cpp`, at the end of the protection loop (after all `bmu_protection_check_battery_ex` calls), add:

```cpp
bmu_protection_publish_snapshot(&prot);
```

This produces snapshots without changing who consumes them yet.

- [ ] **Step 6: Wire queues in app_main**

After `bmu_protection_init()` in `main.cpp`:

```cpp
bmu_protection_queues_t prot_queues = {
    .q_balancer = s_q_balancer,
    .q_display  = s_q_display,
    .q_cloud    = s_q_cloud,
    .q_ble      = s_q_ble,
    .q_cmd      = s_q_cmd,
};
bmu_protection_set_queues(&prot, &prot_queues);
```

- [ ] **Step 7: Build and test**

Run:
```bash
cd firmware-idf && idf.py build
```
Expected: Build succeeds. Snapshots are produced but not consumed yet.

- [ ] **Step 8: Commit**

```bash
git add components/bmu_protection/ main/
git commit -m "feat(protection): produce snapshots each cycle"
```

---

### Task 9: Add cmd queue processing to protection

**Files:**
- Modify: `components/bmu_protection/bmu_protection.cpp`

- [ ] **Step 1: Add cmd processing function**

Add to `bmu_protection.cpp`:

```cpp
static void protection_process_commands(bmu_protection_ctx_t *ctx) {
    bmu_cmd_t cmd;
    // Drain all pending commands (non-blocking)
    while (xQueueReceive(ctx->queues.q_cmd, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
        case CMD_WEB_SWITCH:
            ESP_LOGI(TAG, "CMD web_switch bat=%d on=%d",
                     cmd.payload.web_switch.battery_idx,
                     cmd.payload.web_switch.on);
            bmu_protection_web_switch(ctx,
                                      cmd.payload.web_switch.battery_idx,
                                      cmd.payload.web_switch.on);
            break;

        case CMD_TOPOLOGY_CHANGED:
            ESP_LOGI(TAG, "CMD topology nb_ina=%d nb_tca=%d",
                     cmd.payload.topology.nb_ina,
                     cmd.payload.topology.nb_tca);
            bmu_protection_update_topology(ctx,
                                           cmd.payload.topology.nb_ina,
                                           cmd.payload.topology.nb_tca);
            break;

        case CMD_BALANCE_REQUEST:
            // Phase 3: will be implemented when balancer becomes a task
            ESP_LOGD(TAG, "CMD balance_req bat=%d on=%d",
                     cmd.payload.balance_req.battery_idx,
                     cmd.payload.balance_req.on);
            break;

        case CMD_CONFIG_UPDATE:
            ESP_LOGI(TAG, "CMD config_update");
            break;

        case CMD_BUS_RECOVERY:
            ESP_LOGW(TAG, "CMD bus_recovery bus=%d",
                     cmd.payload.bus_recovery.bus_id);
            bmu_i2c_bus_recover();
            break;
        }
    }
}
```

- [ ] **Step 2: Call process_commands at start of main loop iteration**

In `main.cpp`, inside the while loop, before the protection checks:

```cpp
// Process pending commands before reading sensors
if (s_q_cmd != NULL) {
    protection_process_commands(&prot);
}
```

Wait — `protection_process_commands` is static in bmu_protection.cpp. Instead, expose a public function.

Add to `bmu_protection.h`:

```cpp
void bmu_protection_process_commands(bmu_protection_ctx_t *ctx);
```

Rename the static function to match and remove `static`.

Then in `main.cpp` main loop, before the battery iteration:

```cpp
bmu_protection_process_commands(&prot);
```

- [ ] **Step 3: Migrate web_switch to use cmd queue**

In the web server handler (find `bmu_protection_web_switch` call in `bmu_web.cpp`), replace the direct call with a queue post:

```cpp
// Before (direct call):
// bmu_protection_web_switch(ctx->prot, battery_idx, on);

// After (post to cmd queue):
extern QueueHandle_t bmu_get_cmd_queue(void);  // expose from main.cpp
bmu_cmd_t cmd = {};
cmd.type = CMD_WEB_SWITCH;
cmd.payload.web_switch.battery_idx = battery_idx;
cmd.payload.web_switch.on = on;
xQueueSend(bmu_get_cmd_queue(), &cmd, pdMS_TO_TICKS(100));
```

Add to `main.cpp`:

```cpp
QueueHandle_t bmu_get_cmd_queue(void) {
    return s_q_cmd;
}
```

- [ ] **Step 4: Build and test**

Run:
```bash
cd firmware-idf && idf.py build
```
Expected: Build succeeds.

- [ ] **Step 5: Commit**

```bash
git add components/bmu_protection/ components/bmu_web/ main/
git commit -m "feat(protection): process cmd queue each cycle"
```

---

### Task 10: Extract protection into dedicated FreeRTOS task

**Files:**
- Modify: `components/bmu_protection/bmu_protection.cpp`
- Modify: `components/bmu_protection/include/bmu_protection.h`
- Modify: `main/main.cpp`

- [ ] **Step 1: Add task creation function to protection header**

```cpp
// Start protection as autonomous FreeRTOS task (prio 8, 100ms period)
esp_err_t bmu_protection_start_task(bmu_protection_ctx_t *ctx,
                                     uint32_t period_ms,
                                     UBaseType_t priority,
                                     uint32_t stack_size);
```

- [ ] **Step 2: Implement protection task loop**

Add to `bmu_protection.cpp`:

```cpp
static void protection_task(void *arg) {
    bmu_protection_ctx_t *ctx = (bmu_protection_ctx_t *)arg;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(ctx->task_period_ms);

    ESP_LOGI(TAG, "Protection task started (period=%lums, prio=%d)",
             ctx->task_period_ms, uxTaskPriorityGet(NULL));

    // Warm-up: 5 cycles to stabilize INA237 voltage cache
    for (int w = 0; w < 5; w++) {
        for (int i = 0; i < ctx->nb_ina; i++) {
            float v, c;
            bmu_ina237_read_voltage_current(&ctx->ina_devices[i], &v, &c);
            if (v > 0) ctx->battery_voltages[i] = v;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    while (true) {
        // 1. Process pending commands
        bmu_protection_process_commands(ctx);

        // 2. Compute fleet max once
        float fleet_max = bmu_protection_compute_fleet_max(ctx);

        // 3. Check each battery
        for (int i = 0; i < ctx->nb_ina; i++) {
            bmu_protection_check_battery_ex(ctx, i, fleet_max);
        }

        // 4. Publish snapshot
        bmu_protection_publish_snapshot(ctx);

        // 5. Wait for next period (hard timing)
        vTaskDelayUntil(&last_wake, period);
    }
}

esp_err_t bmu_protection_start_task(bmu_protection_ctx_t *ctx,
                                     uint32_t period_ms,
                                     UBaseType_t priority,
                                     uint32_t stack_size) {
    ctx->task_period_ms = period_ms;
    BaseType_t ret = xTaskCreate(protection_task, "protection",
                                  stack_size, ctx, priority, &ctx->task_handle);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
```

- [ ] **Step 3: Add task fields to context struct**

In `bmu_protection.h`, add to `bmu_protection_ctx_t`:

```cpp
uint32_t     task_period_ms;
TaskHandle_t task_handle;
```

- [ ] **Step 4: Replace main loop with task start in main.cpp**

In `main.cpp`, replace the entire `while(true)` protection loop with:

```cpp
// Start protection as autonomous task
bmu_protection_start_task(&prot, BMU_LOOP_PERIOD_MS, 8, 8192);
ESP_LOGI(TAG, "Protection task launched");

// Main task can now sleep or handle other duties
while (true) {
    // Display update: read latest snapshot from queue
    bmu_snapshot_t snap;
    if (xQueuePeek(s_q_display, &snap, pdMS_TO_TICKS(500)) == pdTRUE) {
        // Update display context with snapshot data
        // (transitional: still using old display API)
    }
    vTaskDelay(pdMS_TO_TICKS(200));
}
```

- [ ] **Step 5: Move warm-up from app_main to protection task**

Remove the warm-up loop from `app_main` (the 5-cycle voltage cache fill). It's now in `protection_task`.

- [ ] **Step 6: Build**

Run:
```bash
cd firmware-idf && idf.py build
```
Expected: Build succeeds.

- [ ] **Step 7: Flash and validate**

Run:
```bash
cd firmware-idf && idf.py -p /dev/cu.usbmodem* flash monitor
```

Expected:
- "Protection task started (period=100ms, prio=8)" in logs
- All 8 batteries detected and cycling
- No watchdog triggers
- Display still updates (via transitional code)

- [ ] **Step 8: Commit**

```bash
git add components/bmu_protection/ main/
git commit -m "feat(protection): autonomous FreeRTOS task prio 8"
```

---

### Task 11: Flash and validate Phase 2

**Files:** None (validation only)

- [ ] **Step 1: Full regression test on device**

Flash and monitor for 5 minutes:
- Protection cycles at 100ms (check with `ESP_LOGD` timestamps)
- Web switch works via cmd queue (test from web UI)
- No I2C contention errors
- Memory usage stable (no leaks from snapshot queue)

- [ ] **Step 2: Run host tests**

```bash
cd firmware-idf/test && make run
```
Expected: All tests pass (existing 13 + new 18).

- [ ] **Step 3: Tag**

```bash
git tag phase2-protection-task
```

---

## Phase 3 — Full decoupling

### Task 12: Balancer as snapshot consumer + BALANCE_REQUEST producer

**Files:**
- Modify: `components/bmu_balancer/include/bmu_balancer.h`
- Modify: `components/bmu_balancer/bmu_balancer.cpp`
- Modify: `components/bmu_balancer/CMakeLists.txt`
- Modify: `main/main.cpp`
- Create: `test/test_balancer_logic/CMakeLists.txt`
- Create: `test/test_balancer_logic/main/CMakeLists.txt`
- Create: `test/test_balancer_logic/main/test_balancer_logic.cpp`

- [ ] **Step 1: Write balancer logic tests (host)**

```cpp
// test/test_balancer_logic/main/test_balancer_logic.cpp
#define NATIVE_TEST
#include <unity.h>
#include <string.h>
#include "bmu_types.h"

// Balancer pure logic (extracted, no FreeRTOS deps)
// Determines which batteries need duty-cycle OFF

#define BAL_HIGH_MV     200.0f   // threshold above mean
#define BAL_DUTY_ON     10       // cycles ON before OFF
#define BAL_DUTY_OFF    5        // cycles OFF
#define BAL_MIN_CONN    2        // min batteries to enable balancing

typedef struct {
    int  on_counter;
    int  off_counter;
    bool balancing;
} bal_state_t;

// Returns true if battery should be switched OFF this cycle
static bool bal_should_disconnect(bal_state_t *bs, float v_mv, float v_mean,
                                   int nb_connected) {
    if (nb_connected < BAL_MIN_CONN) return false;

    if (bs->off_counter > 0) {
        bs->off_counter--;
        if (bs->off_counter == 0) {
            bs->balancing = false;
            bs->on_counter = BAL_DUTY_ON;
        }
        return false;  // stay OFF, already disconnected
    }

    float delta = v_mv - v_mean;
    if (delta > BAL_HIGH_MV) {
        bs->on_counter--;
        if (bs->on_counter <= 0) {
            bs->balancing = true;
            bs->off_counter = BAL_DUTY_OFF;
            return true;  // disconnect now
        }
    } else {
        bs->on_counter = BAL_DUTY_ON;
        bs->balancing = false;
    }
    return false;
}

void setUp(void) {}
void tearDown(void) {}

void test_bal_no_action_below_threshold(void) {
    bal_state_t bs = { .on_counter = BAL_DUTY_ON, .off_counter = 0, .balancing = false };
    bool disc = bal_should_disconnect(&bs, 27500.0f, 27400.0f, 4);
    TEST_ASSERT_FALSE(disc);
    TEST_ASSERT_FALSE(bs.balancing);
}

void test_bal_no_action_too_few_batteries(void) {
    bal_state_t bs = { .on_counter = BAL_DUTY_ON, .off_counter = 0, .balancing = false };
    bool disc = bal_should_disconnect(&bs, 28000.0f, 27000.0f, 1);
    TEST_ASSERT_FALSE(disc);
}

void test_bal_disconnect_after_on_counter_expires(void) {
    bal_state_t bs = { .on_counter = 1, .off_counter = 0, .balancing = false };
    // v_mv - v_mean = 300 > 200 (threshold)
    bool disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
    TEST_ASSERT_TRUE(disc);
    TEST_ASSERT_TRUE(bs.balancing);
    TEST_ASSERT_EQUAL_INT(BAL_DUTY_OFF, bs.off_counter);
}

void test_bal_off_counter_counts_down(void) {
    bal_state_t bs = { .on_counter = 0, .off_counter = 3, .balancing = true };
    bool disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
    TEST_ASSERT_FALSE(disc);  // already OFF, just counting down
    TEST_ASSERT_EQUAL_INT(2, bs.off_counter);
    TEST_ASSERT_TRUE(bs.balancing);
}

void test_bal_reconnect_after_off_expires(void) {
    bal_state_t bs = { .on_counter = 0, .off_counter = 1, .balancing = true };
    bool disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
    TEST_ASSERT_FALSE(disc);
    TEST_ASSERT_EQUAL_INT(0, bs.off_counter);
    TEST_ASSERT_FALSE(bs.balancing);
    TEST_ASSERT_EQUAL_INT(BAL_DUTY_ON, bs.on_counter);  // reset
}

void test_bal_reset_counter_when_voltage_drops(void) {
    bal_state_t bs = { .on_counter = 3, .off_counter = 0, .balancing = false };
    // Voltage dropped below threshold (delta = 50 < 200)
    bool disc = bal_should_disconnect(&bs, 27250.0f, 27200.0f, 4);
    TEST_ASSERT_FALSE(disc);
    TEST_ASSERT_EQUAL_INT(BAL_DUTY_ON, bs.on_counter);  // reset to full
}

void test_bal_full_cycle(void) {
    bal_state_t bs = { .on_counter = BAL_DUTY_ON, .off_counter = 0, .balancing = false };

    // Count down on_counter with sustained high voltage
    for (int i = 0; i < BAL_DUTY_ON - 1; i++) {
        bool disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
        TEST_ASSERT_FALSE(disc);
    }
    // Last tick → disconnect
    bool disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
    TEST_ASSERT_TRUE(disc);
    TEST_ASSERT_TRUE(bs.balancing);

    // Count down off_counter
    for (int i = 0; i < BAL_DUTY_OFF; i++) {
        disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
        TEST_ASSERT_FALSE(disc);
    }
    // After off expires: ready for next cycle
    TEST_ASSERT_FALSE(bs.balancing);
    TEST_ASSERT_EQUAL_INT(BAL_DUTY_ON, bs.on_counter);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bal_no_action_below_threshold);
    RUN_TEST(test_bal_no_action_too_few_batteries);
    RUN_TEST(test_bal_disconnect_after_on_counter_expires);
    RUN_TEST(test_bal_off_counter_counts_down);
    RUN_TEST(test_bal_reconnect_after_off_expires);
    RUN_TEST(test_bal_reset_counter_when_voltage_drops);
    RUN_TEST(test_bal_full_cycle);
    return UNITY_END();
}
```

- [ ] **Step 2: Create test CMakeLists + Makefile entry**

```cmake
# test/test_balancer_logic/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_balancer_logic)
```

```cmake
# test/test_balancer_logic/main/CMakeLists.txt
idf_component_register(
    SRCS "test_balancer_logic.cpp"
    INCLUDE_DIRS "."
    REQUIRES unity
)
```

Add to `test/Makefile`:

```makefile
TESTS += test_balancer_logic

build/test_balancer_logic: test_balancer_logic/main/test_balancer_logic.cpp $(UNITY_SRC)
	$(CXX) $(CXXFLAGS) -I../components/bmu_types/include -I$(UNITY_DIR)/src \
		$< $(UNITY_SRC) -o $@
```

- [ ] **Step 3: Run tests**

```bash
cd firmware-idf/test && make build/test_balancer_logic && ./build/test_balancer_logic
```
Expected: 7 tests, 0 failures.

- [ ] **Step 4: Commit tests**

```bash
git add test/test_balancer_logic/ test/Makefile
git commit -m "test: balancer duty-cycle logic (7 tests)"
```

- [ ] **Step 5: Refactor balancer to consume snapshot + produce cmd**

Rewrite `bmu_balancer.h`:

```cpp
#pragma once
#include "bmu_types.h"

typedef struct {
    QueueHandle_t q_snapshot;  // input: snapshot from protection
    QueueHandle_t q_cmd;       // output: BALANCE_REQUEST to protection
} bmu_balancer_config_t;

esp_err_t bmu_balancer_init(const bmu_balancer_config_t *cfg);
esp_err_t bmu_balancer_start_task(UBaseType_t priority, uint32_t stack_size);
bool      bmu_balancer_is_off(uint8_t idx);
int       bmu_balancer_get_duty_pct(uint8_t idx);
```

Rewrite `bmu_balancer.cpp` core task:

```cpp
static bmu_balancer_config_t s_cfg;

static struct {
    int  on_counter;
    int  off_counter;
    bool balancing;
    float v_before_mv;
    float i_before_a;
} s_bat[BMU_MAX_BATTERIES];

static void balancer_task(void *arg) {
    ESP_LOGI(TAG, "Balancer task started");

    while (true) {
        bmu_snapshot_t snap;
        // Block until a new snapshot arrives
        if (xQueueReceive(s_cfg.q_snapshot, &snap, portMAX_DELAY) != pdTRUE)
            continue;

        if (snap.nb_batteries < CONFIG_BMU_BALANCE_MIN_CONNECTED)
            continue;

        float v_mean = snap.fleet_mean_mv;
        int nb_connected = 0;
        for (int i = 0; i < snap.nb_batteries; i++) {
            if (snap.battery[i].state == BMU_STATE_CONNECTED)
                nb_connected++;
        }

        for (int i = 0; i < snap.nb_batteries; i++) {
            if (snap.battery[i].state != BMU_STATE_CONNECTED)
                continue;

            float v = snap.battery[i].voltage_mv;
            bool should_off = false;

            if (s_bat[i].off_counter > 0) {
                s_bat[i].off_counter--;
                if (s_bat[i].off_counter == 0) {
                    s_bat[i].balancing = false;
                    s_bat[i].on_counter = CONFIG_BMU_BALANCE_DUTY_ON;
                    // Request reconnect
                    bmu_cmd_t cmd = {};
                    cmd.type = CMD_BALANCE_REQUEST;
                    cmd.payload.balance_req.battery_idx = i;
                    cmd.payload.balance_req.on = true;
                    xQueueSend(s_cfg.q_cmd, &cmd, pdMS_TO_TICKS(50));
                }
                continue;
            }

            float delta = v - v_mean;
            if (delta > CONFIG_BMU_BALANCE_HIGH_MV) {
                s_bat[i].on_counter--;
                if (s_bat[i].on_counter <= 0) {
                    should_off = true;
                    s_bat[i].balancing = true;
                    s_bat[i].off_counter = CONFIG_BMU_BALANCE_DUTY_OFF;
                    s_bat[i].v_before_mv = v;
                    s_bat[i].i_before_a = snap.battery[i].current_a;
                }
            } else {
                s_bat[i].on_counter = CONFIG_BMU_BALANCE_DUTY_ON;
                s_bat[i].balancing = false;
            }

            if (should_off) {
                bmu_cmd_t cmd = {};
                cmd.type = CMD_BALANCE_REQUEST;
                cmd.payload.balance_req.battery_idx = i;
                cmd.payload.balance_req.on = false;
                xQueueSend(s_cfg.q_cmd, &cmd, pdMS_TO_TICKS(50));
            }
        }
    }
}

esp_err_t bmu_balancer_init(const bmu_balancer_config_t *cfg) {
    s_cfg = *cfg;
    for (int i = 0; i < BMU_MAX_BATTERIES; i++) {
        s_bat[i].on_counter = CONFIG_BMU_BALANCE_DUTY_ON;
        s_bat[i].off_counter = 0;
        s_bat[i].balancing = false;
    }
    return ESP_OK;
}

esp_err_t bmu_balancer_start_task(UBaseType_t priority, uint32_t stack_size) {
    BaseType_t ret = xTaskCreate(balancer_task, "balancer",
                                  stack_size, NULL, priority, NULL);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}
```

- [ ] **Step 6: Handle CMD_BALANCE_REQUEST in protection**

In `bmu_protection.cpp`, in `bmu_protection_process_commands()`, implement the `CMD_BALANCE_REQUEST` case:

```cpp
case CMD_BALANCE_REQUEST: {
    uint8_t idx = cmd.payload.balance_req.battery_idx;
    bool on = cmd.payload.balance_req.on;
    ESP_LOGD(TAG, "CMD balance bat=%d on=%d", idx, on);

    if (idx >= ctx->nb_ina) break;

    xSemaphoreTake(ctx->state_mutex, pdMS_TO_TICKS(20));
    // Only allow balance actions on CONNECTED batteries
    if (ctx->battery_state[idx] == BMU_STATE_CONNECTED) {
        switch_battery(ctx, idx, on);
    }
    xSemaphoreGive(ctx->state_mutex);
    break;
}
```

- [ ] **Step 7: Wire balancer in main.cpp**

Replace old `bmu_balancer_init(&prot)` + `bmu_balancer_tick()` with:

```cpp
bmu_balancer_config_t bal_cfg = {
    .q_snapshot = s_q_balancer,
    .q_cmd = s_q_cmd,
};
bmu_balancer_init(&bal_cfg);
bmu_balancer_start_task(5, 4096);
```

Remove `bmu_balancer_tick()` call from the old main loop (which no longer exists after Task 10).

- [ ] **Step 8: Build and test**

```bash
cd firmware-idf && idf.py build
cd firmware-idf/test && make run
```
Expected: Build succeeds, all host tests pass.

- [ ] **Step 9: Commit**

```bash
git add components/bmu_balancer/ components/bmu_protection/ main/ test/
git commit -m "feat(balancer): autonomous task consuming snapshots"
```

---

### Task 13: Cloud telemetry consumes snapshot

**Files:**
- Modify: `main/main.cpp` (cloud_telemetry_task)

- [ ] **Step 1: Refactor cloud_telemetry_task to read from queue**

Replace the existing cloud_telemetry_task context and loop:

```cpp
typedef struct {
    QueueHandle_t          q_snapshot;
    bmu_battery_manager_t *mgr;
} cloud_task_ctx_t;

static void cloud_telemetry_task(void *arg) {
    cloud_task_ctx_t *ctx = (cloud_task_ctx_t *)arg;
    bmu_snapshot_t snap = {};

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // 10s period

        // Peek latest snapshot (non-destructive)
        if (xQueuePeek(ctx->q_snapshot, &snap, 0) != pdTRUE)
            continue;

        for (int i = 0; i < snap.nb_batteries; i++) {
            float ah_d = bmu_battery_manager_get_ah_discharge(ctx->mgr, i);
            float ah_c = bmu_battery_manager_get_ah_charge(ctx->mgr, i);

            const char *state_str = "unknown";
            switch (snap.battery[i].state) {
                case BMU_STATE_CONNECTED:    state_str = "connected"; break;
                case BMU_STATE_DISCONNECTED: state_str = "disconnected"; break;
                case BMU_STATE_RECONNECTING: state_str = "reconnecting"; break;
                case BMU_STATE_ERROR:        state_str = "error"; break;
                case BMU_STATE_LOCKED:       state_str = "locked"; break;
            }

            bmu_influx_write_battery(i,
                snap.battery[i].voltage_mv,
                snap.battery[i].current_a,
                ah_d, ah_c, state_str);
        }

        // MQTT publish fleet summary
        bmu_mqtt_publish_status(snap.nb_batteries, snap.fleet_max_mv,
                                 snap.topology_ok);
    }
}
```

- [ ] **Step 2: Update cloud task creation in app_main**

```cpp
static cloud_task_ctx_t cloud_ctx = {
    .q_snapshot = s_q_cloud,
    .mgr = &mgr,
};
xTaskCreate(cloud_telemetry_task, "cloud", 4096, &cloud_ctx, 2, NULL);
```

- [ ] **Step 3: Build and test**

```bash
cd firmware-idf && idf.py build
```

- [ ] **Step 4: Commit**

```bash
git add main/
git commit -m "feat(cloud): consume snapshot instead of polling"
```

---

### Task 14: Hotplug posts CMD_TOPOLOGY_CHANGED

**Files:**
- Modify: `components/bmu_i2c_hotplug/include/bmu_i2c_hotplug.h`
- Modify: `components/bmu_i2c_hotplug/bmu_i2c_hotplug.cpp`
- Modify: `components/bmu_i2c_hotplug/CMakeLists.txt`
- Modify: `main/main.cpp`

- [ ] **Step 1: Add cmd queue to hotplug config**

In `bmu_i2c_hotplug.h`, add to `bmu_hotplug_cfg_t`:

```cpp
#include "bmu_types.h"

// Add field:
QueueHandle_t q_cmd;  // posts CMD_TOPOLOGY_CHANGED
```

Add `bmu_types` to hotplug CMakeLists REQUIRES.

- [ ] **Step 2: Replace direct topology mutation with queue post**

In `bmu_i2c_hotplug.cpp`, find where `bmu_protection_update_topology()` is called. Replace with:

```cpp
// Before:
// bmu_protection_update_topology(s_cfg.prot, new_nb_ina, new_nb_tca);

// After:
if (s_cfg.q_cmd) {
    bmu_cmd_t cmd = {};
    cmd.type = CMD_TOPOLOGY_CHANGED;
    cmd.payload.topology.nb_ina = *s_cfg.nb_ina;
    cmd.payload.topology.nb_tca = *s_cfg.nb_tca;
    xQueueSend(s_cfg.q_cmd, &cmd, pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Posted CMD_TOPOLOGY_CHANGED (ina=%d tca=%d)",
             cmd.payload.topology.nb_ina, cmd.payload.topology.nb_tca);
}
```

- [ ] **Step 3: Wire cmd queue in main.cpp hotplug config**

```cpp
hotplug_cfg.q_cmd = s_q_cmd;
```

- [ ] **Step 4: Build and test**

```bash
cd firmware-idf && idf.py build
```

- [ ] **Step 5: Commit**

```bash
git add components/bmu_i2c_hotplug/ main/
git commit -m "feat(hotplug): post CMD_TOPOLOGY_CHANGED via queue"
```

---

### Task 15: Display consumes snapshot

**Files:**
- Modify: `components/bmu_display/bmu_ui_detail.cpp`
- Modify: `components/bmu_display/bmu_display.cpp` (or wherever display task runs)
- Modify: `components/bmu_display/CMakeLists.txt`

- [ ] **Step 1: Add snapshot queue to display context**

In the display context struct (check exact file), add:

```cpp
#include "bmu_types.h"

// Add to bmu_display_ctx_t or bmu_ui_ctx_t:
QueueHandle_t q_snapshot;
bmu_snapshot_t last_snap;  // cached for rendering
```

- [ ] **Step 2: Update display update to read from snapshot**

In the display update function, replace direct protection reads:

```cpp
// Before:
// float v_mv = bmu_protection_get_voltage(ctx->prot, idx);
// bmu_battery_state_t state = bmu_protection_get_state(ctx->prot, idx);

// After:
// Peek latest snapshot (non-blocking)
bmu_snapshot_t snap;
if (xQueuePeek(ctx->q_snapshot, &snap, 0) == pdTRUE) {
    ctx->last_snap = snap;
}
float v_mv = ctx->last_snap.battery[idx].voltage_mv;
bmu_battery_state_t state = ctx->last_snap.battery[idx].state;
```

- [ ] **Step 3: Wire snapshot queue in main.cpp**

```cpp
display_ctx.q_snapshot = s_q_display;
```

- [ ] **Step 4: Build and test**

```bash
cd firmware-idf && idf.py build
```

- [ ] **Step 5: Flash and verify display works**

```bash
cd firmware-idf && idf.py -p /dev/cu.usbmodem* flash monitor
```
Expected: Display shows battery data from snapshots. Same visual result.

- [ ] **Step 6: Commit**

```bash
git add components/bmu_display/ main/
git commit -m "feat(display): consume snapshot from queue"
```

---

### Task 16: Integrate health score in protection cycle

**Files:**
- Modify: `components/bmu_protection/bmu_protection.cpp`
- Modify: `components/bmu_protection/include/bmu_protection.h`

- [ ] **Step 1: Add health arrays to protection context**

In `bmu_protection_ctx_t`:

```cpp
bmu_device_health_t  ina_health[BMU_MAX_BATTERIES];
```

Initialize all to `{ .score = 100, .consec_fails = 0 }` in `bmu_protection_init()`.

- [ ] **Step 2: Update check_battery_ex to track health**

In `bmu_protection_check_battery_ex()`, after `bmu_ina237_read_voltage_current()`:

```cpp
float v_mv = 0, i_a = 0;
esp_err_t ret = bmu_ina237_read_voltage_current(&ctx->ina_devices[battery_idx], &v_mv, &i_a);

if (ret != ESP_OK) {
    bmu_i2c_health_record_failure(&ctx->ina_health[battery_idx]);

    // If critical: force battery OFF
    if (bmu_i2c_health_is_critical(&ctx->ina_health[battery_idx])) {
        if (ctx->battery_state[battery_idx] == BMU_STATE_CONNECTED) {
            ESP_LOGW(TAG, "Bat %d health critical (score=%d), forcing OFF",
                     battery_idx, ctx->ina_health[battery_idx].score);
            switch_battery(ctx, battery_idx, false);
            ctx->battery_state[battery_idx] = BMU_STATE_DISCONNECTED;
        }
    }
    return ESP_FAIL;  // skip rest of checks
}

bmu_i2c_health_record_success(&ctx->ina_health[battery_idx]);

// Reconnect hysteresis: only reconnect if health score recovered
if (ctx->battery_state[battery_idx] == BMU_STATE_DISCONNECTED &&
    ctx->ina_health[battery_idx].score < BMU_HEALTH_THRESH_RECONNECT) {
    return ESP_OK;  // don't reconnect yet, score too low
}
```

- [ ] **Step 3: Include health score in snapshot**

In `bmu_protection_publish_snapshot()`:

```cpp
snap.battery[i].health_score = ctx->ina_health[i].score;
```

- [ ] **Step 4: Build and test**

```bash
cd firmware-idf && idf.py build
cd firmware-idf/test && make run
```

- [ ] **Step 5: Commit**

```bash
git add components/bmu_protection/
git commit -m "feat(protection): per-battery health score with hysteresis"
```

---

### Task 17: Flash and validate Phase 3

**Files:** None (validation only)

- [ ] **Step 1: Full build + flash**

```bash
cd firmware-idf && idf.py build && idf.py -p /dev/cu.usbmodem* flash monitor
```

- [ ] **Step 2: Validate for 5 minutes**

Check:
- Protection task running at 100ms (timestamps in logs)
- Balancer task receiving snapshots (log "Balancer task started")
- Cloud telemetry posting from snapshots every 10s
- Hotplug posting CMD_TOPOLOGY_CHANGED (unplug/replug a battery if possible)
- Display updating from snapshot queue
- Health scores all at 100 (no I2C errors)
- No watchdog, no crash, no memory leak

- [ ] **Step 3: Run all host tests**

```bash
cd firmware-idf/test && make run
```
Expected: All tests pass (13 original + 10 health + 8 snapshot + 7 balancer = 38).

- [ ] **Step 4: Tag**

```bash
git tag phase3-full-decoupling
```

---

## Phase 4 — Dual-bus 32 batteries

### Task 18: Refactor I2C to multi-bus abstraction

**Files:**
- Modify: `components/bmu_i2c/include/bmu_i2c.h`
- Modify: `components/bmu_i2c/bmu_i2c.cpp`

- [ ] **Step 1: Add multi-bus storage**

```cpp
// In bmu_i2c.cpp:
static bmu_i2c_bus_t s_buses[BMU_MAX_BUSES];
static uint8_t s_nb_buses = 0;

// New init function:
esp_err_t bmu_i2c_bus_init(uint8_t bus_id, i2c_master_bus_handle_t handle) {
    if (bus_id >= BMU_MAX_BUSES) return ESP_ERR_INVALID_ARG;

    s_buses[bus_id].handle = handle;
    s_buses[bus_id].bus_id = bus_id;
    s_buses[bus_id].bus_health.score = BMU_HEALTH_SCORE_INIT;
    s_buses[bus_id].bus_health.consec_fails = 0;
    s_buses[bus_id].mutex = xSemaphoreCreateMutex();

    if (!s_buses[bus_id].mutex) return ESP_ERR_NO_MEM;

    if (bus_id >= s_nb_buses) s_nb_buses = bus_id + 1;
    ESP_LOGI(TAG, "I2C bus %d initialized", bus_id);
    return ESP_OK;
}
```

- [ ] **Step 2: Add bus-specific lock/unlock**

```cpp
esp_err_t bmu_i2c_bus_lock(uint8_t bus_id) {
    if (bus_id >= s_nb_buses) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_buses[bus_id].mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

void bmu_i2c_bus_unlock(uint8_t bus_id) {
    if (bus_id < s_nb_buses)
        xSemaphoreGive(s_buses[bus_id].mutex);
}

bmu_i2c_bus_t* bmu_i2c_get_bus(uint8_t bus_id) {
    if (bus_id >= s_nb_buses) return NULL;
    return &s_buses[bus_id];
}

uint8_t bmu_i2c_get_nb_buses(void) {
    return s_nb_buses;
}
```

- [ ] **Step 3: Keep backward-compatible wrapper**

The existing `bmu_i2c_lock()`/`bmu_i2c_unlock()` continue to work on bus 0:

```cpp
esp_err_t bmu_i2c_lock(void) { return bmu_i2c_bus_lock(0); }
void bmu_i2c_unlock(void)    { bmu_i2c_bus_unlock(0); }
```

- [ ] **Step 4: Update header**

Add new declarations to `bmu_i2c.h`:

```cpp
esp_err_t bmu_i2c_bus_init(uint8_t bus_id, i2c_master_bus_handle_t handle);
esp_err_t bmu_i2c_bus_lock(uint8_t bus_id);
void      bmu_i2c_bus_unlock(uint8_t bus_id);
bmu_i2c_bus_t* bmu_i2c_get_bus(uint8_t bus_id);
uint8_t   bmu_i2c_get_nb_buses(void);
```

- [ ] **Step 5: Build (backward-compatible, bus 0 only)**

```bash
cd firmware-idf && idf.py build
```
Expected: Build succeeds. Existing code uses bus 0 via old API.

- [ ] **Step 6: Commit**

```bash
git add components/bmu_i2c/
git commit -m "feat(i2c): multi-bus abstraction with per-bus mutex"
```

---

### Task 19: Add global_idx mapping to INA237 and TCA9535

**Files:**
- Modify: `components/bmu_ina237/include/bmu_ina237.h`
- Modify: `components/bmu_ina237/bmu_ina237.cpp`
- Modify: `components/bmu_tca9535/include/bmu_tca9535.h`
- Modify: `components/bmu_tca9535/bmu_tca9535.cpp`

- [ ] **Step 1: Add global_idx field to INA237 context**

In `bmu_ina237_t`:

```cpp
uint8_t global_idx;  // 0-31 across all buses
uint8_t bus_id;      // which I2C bus
```

- [ ] **Step 2: Add global_idx field to TCA9535 handle**

In `bmu_tca9535_handle_t`:

```cpp
uint8_t global_idx;  // base global index (this TCA controls global_idx..global_idx+3)
uint8_t bus_id;
```

- [ ] **Step 3: Set global_idx during scan_init**

In `bmu_ina237_scan_init()`, add a `bus_id` and `global_offset` parameter:

```cpp
esp_err_t bmu_ina237_scan_init(i2c_master_bus_handle_t bus,
                               uint32_t r_shunt_uohm, float max_current_a,
                               bmu_ina237_t devices[], uint8_t *count,
                               uint8_t bus_id, uint8_t global_offset);
```

Inside the scan loop:

```cpp
devices[*count].global_idx = global_offset + *count;
devices[*count].bus_id = bus_id;
```

Do the same for `bmu_tca9535_scan_init()`.

- [ ] **Step 4: Update all callers to pass bus_id=0, global_offset=0**

In `main.cpp` and `bmu_i2c_hotplug.cpp`, add the new parameters with default bus 0 values.

- [ ] **Step 5: Build**

```bash
cd firmware-idf && idf.py build
```
Expected: Build succeeds. All devices on bus 0, global_idx = local_idx.

- [ ] **Step 6: Commit**

```bash
git add components/bmu_ina237/ components/bmu_tca9535/ main/ components/bmu_i2c_hotplug/
git commit -m "feat: global_idx mapping for dual-bus support"
```

---

### Task 20: Dual-bus scan and topology validation

**Files:**
- Modify: `components/bmu_i2c_hotplug/bmu_i2c_hotplug.cpp`
- Modify: `main/main.cpp`

- [ ] **Step 1: Add bus 1 initialization in main.cpp**

After bus 0 init, conditionally init bus 1:

```cpp
#if defined(CONFIG_BMU_I2C_BITBANG_ENABLED)
    i2c_master_bus_handle_t bus1_handle = NULL;
    // bmu_i2c_bitbang already creates this handle
    extern i2c_master_bus_handle_t bmu_i2c_bitbang_get_handle(void);
    bus1_handle = bmu_i2c_bitbang_get_handle();
    if (bus1_handle) {
        bmu_i2c_bus_init(1, bus1_handle);
        ESP_LOGI(TAG, "Bus 1 (bitbang) registered");
    }
#endif
```

- [ ] **Step 2: Add dual-bus scan in hotplug**

In hotplug_task, after scanning bus 0, scan bus 1 if available:

```cpp
uint8_t nb_buses = bmu_i2c_get_nb_buses();
for (uint8_t bus = 0; bus < nb_buses; bus++) {
    bmu_i2c_bus_t *b = bmu_i2c_get_bus(bus);
    if (!b) continue;

    uint8_t offset = bus * 16;  // bus 0: 0-15, bus 1: 16-31
    scan_new_ina(b->handle, bus, offset);
    scan_new_tca(b->handle, bus, offset);
}
```

- [ ] **Step 3: Global topology validation**

```cpp
// Total across all buses
uint8_t total_ina = 0, total_tca = 0;
for (uint8_t bus = 0; bus < nb_buses; bus++) {
    total_ina += bus_ina_count[bus];
    total_tca += bus_tca_count[bus];
}
bool topo_ok = (total_tca * 4 == total_ina);
```

- [ ] **Step 4: Build**

```bash
cd firmware-idf && idf.py build
```
Expected: Build succeeds. With bus 1 disabled, behavior unchanged.

- [ ] **Step 5: Commit**

```bash
git add components/bmu_i2c_hotplug/ main/
git commit -m "feat(hotplug): dual-bus scan with global topology validation"
```

---

### Task 21: Flash and validate Phase 4

**Files:** None (validation only)

- [ ] **Step 1: Build and flash**

```bash
cd firmware-idf && idf.py build && idf.py -p /dev/cu.usbmodem* flash monitor
```

- [ ] **Step 2: Validate with 8 batteries (single bus)**

Expected: Identical behavior to Phase 3. Bus 1 not activated (no bitbang GPIOs configured).

- [ ] **Step 3: Run all host tests**

```bash
cd firmware-idf/test && make run
```
Expected: All 38 tests pass.

- [ ] **Step 4: Tag**

```bash
git tag phase4-dual-bus-ready
```

- [ ] **Step 5: Commit final documentation update**

Update CLAUDE.md with new architecture notes (queues, task priorities, health scores).

```bash
git add CLAUDE.md
git commit -m "docs: update architecture notes for RTOS redesign"
```
