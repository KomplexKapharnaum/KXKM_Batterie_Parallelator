# Victron Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate SmartSolar 150/35 via VE.Direct, publish to Victron VRM cloud, and expose BMU+Solar as Victron devices via BLE Instant Readout — all on ESP32-S3-BOX-3.

**Architecture:** Three decoupled layers sharing `bmu_vedirect_data_t`. Layer 1 (Data) activates VE.Direct and adds solar telemetry to existing MQTT/InfluxDB. Layer 2 (Cloud) creates a new `bmu_vrm` component publishing to `mqtt.victronenergy.com`. Layer 3 (BLE) creates `bmu_ble_victron` for Victron-format advertising. Layers 2 and 3 depend only on Layer 1.

**Tech Stack:** ESP-IDF v5.4, NimBLE, esp_mqtt (TLS), mbedtls AES-CTR, LVGL v9

**Spec:** `docs/superpowers/specs/2026-04-02-victron-integration-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `firmware-idf/sdkconfig.defaults` | Modify | Enable VE.Direct, set GPIOs, add VRM/BLE Victron defaults |
| `firmware-idf/components/bmu_vedirect/Kconfig` | Modify | Change default GPIOs to 39/38, UART to 2 |
| `firmware-idf/main/main.cpp` | Modify | Add solar telemetry in cloud task, init VRM + BLE Victron |
| `firmware-idf/components/bmu_vrm/include/bmu_vrm.h` | Create | VRM client public API |
| `firmware-idf/components/bmu_vrm/bmu_vrm.cpp` | Create | MQTT client to mqtt.victronenergy.com |
| `firmware-idf/components/bmu_vrm/Kconfig` | Create | VRM config: portal ID, enable, TLS, interval |
| `firmware-idf/components/bmu_vrm/CMakeLists.txt` | Create | Component registration |
| `firmware-idf/components/bmu_ble_victron/include/bmu_ble_victron.h` | Create | Victron BLE public API |
| `firmware-idf/components/bmu_ble_victron/bmu_ble_victron.cpp` | Create | Advertising with Victron Instant Readout |
| `firmware-idf/components/bmu_ble_victron/Kconfig` | Create | BLE Victron config: enable, key, interval |
| `firmware-idf/components/bmu_ble_victron/CMakeLists.txt` | Create | Component registration |
| `firmware-idf/components/bmu_config/include/bmu_config.h` | Modify | Add VRM + BLE Victron config getters/setters |
| `firmware-idf/components/bmu_config/bmu_config.cpp` | Modify | NVS persistence for VRM/BLE Victron settings |
| `firmware-idf/components/bmu_display/bmu_ui_config.cpp` | Modify | Add VICTRON section to config screen |

---

### Task 1: Activate VE.Direct — GPIO 39/38, UART2

**Files:**
- Modify: `firmware-idf/components/bmu_vedirect/Kconfig`
- Modify: `firmware-idf/sdkconfig.defaults`

- [ ] **Step 1: Update Kconfig defaults**

In `firmware-idf/components/bmu_vedirect/Kconfig`, change the defaults:

```
menu "BMU VE.Direct (Victron)"
    config BMU_VEDIRECT_ENABLED
        bool "Enable VE.Direct parser"
        default n
    config BMU_VEDIRECT_UART_NUM
        int "UART number (0-2)"
        default 2
        depends on BMU_VEDIRECT_ENABLED
    config BMU_VEDIRECT_TX_GPIO
        int "TX GPIO (BMU to charger, -1 to disable)"
        default 38
        depends on BMU_VEDIRECT_ENABLED
    config BMU_VEDIRECT_RX_GPIO
        int "RX GPIO (charger to BMU)"
        default 39
        depends on BMU_VEDIRECT_ENABLED
    config BMU_VEDIRECT_BAUD
        int "Baud rate"
        default 19200
        depends on BMU_VEDIRECT_ENABLED
endmenu
```

- [ ] **Step 2: Enable VE.Direct in sdkconfig.defaults**

Append to `firmware-idf/sdkconfig.defaults`:

```
# ── VE.Direct (SmartSolar 150/35) ────────────────────────────────────
CONFIG_BMU_VEDIRECT_ENABLED=y
CONFIG_BMU_VEDIRECT_UART_NUM=2
CONFIG_BMU_VEDIRECT_RX_GPIO=39
CONFIG_BMU_VEDIRECT_TX_GPIO=38
CONFIG_BMU_VEDIRECT_BAUD=19200
```

- [ ] **Step 3: Build to verify VE.Direct compiles**

```bash
cd firmware-idf && rm -rf build/sdkconfig
export IDF_PATH=$HOME/esp/esp-idf && . $IDF_PATH/export.sh
idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_vedirect/Kconfig firmware-idf/sdkconfig.defaults
git commit -m "feat(vedirect): enable SmartSolar on UART2 GPIO39/38"
```

---

### Task 2: Solar telemetry — MQTT + InfluxDB

**Files:**
- Modify: `firmware-idf/main/main.cpp`

- [ ] **Step 1: Add solar publish in cloud_telemetry_task**

In `firmware-idf/main/main.cpp`, add `#include "bmu_vedirect.h"` at the top (already present), then add this block inside `cloud_telemetry_task` after `bmu_influx_flush();` (at the end of the for loop):

```cpp
        /* ── Solar telemetry ── */
        if (bmu_vedirect_is_connected()) {
            const bmu_vedirect_data_t *sol = bmu_vedirect_get_data();
            if (sol && sol->valid) {
                /* MQTT */
                char solar_payload[192];
                snprintf(solar_payload, sizeof(solar_payload),
                    "{\"vpv\":%.1f,\"ppv\":%u,\"vbat\":%.2f,"
                    "\"ibat\":%.2f,\"cs\":\"%s\",\"yield\":%lu,\"err\":%u}",
                    sol->panel_voltage_v,
                    (unsigned)sol->panel_power_w,
                    sol->battery_voltage_v,
                    sol->battery_current_a,
                    bmu_vedirect_cs_name(sol->charge_state),
                    (unsigned long)sol->yield_today_wh,
                    (unsigned)sol->error_code);
                char solar_topic[64];
                snprintf(solar_topic, sizeof(solar_topic),
                    "bmu/%s/solar", bmu_config_get_device_name());
                bmu_mqtt_publish(solar_topic, solar_payload, 0, 0, false);

                /* InfluxDB */
                char solar_tags[48];
                snprintf(solar_tags, sizeof(solar_tags),
                    "device=%s", bmu_config_get_device_name());
                char solar_fields[128];
                snprintf(solar_fields, sizeof(solar_fields),
                    "vpv=%.1f,ppv=%ui,vbat=%.2f,ibat=%.2f,cs=%ui,yield=%lui",
                    sol->panel_voltage_v,
                    (unsigned)sol->panel_power_w,
                    sol->battery_voltage_v,
                    sol->battery_current_a,
                    (unsigned)sol->charge_state,
                    (unsigned long)sol->yield_today_wh);
                bmu_influx_write("solar", solar_tags, solar_fields, 0);
            }
        }
```

This goes right after the existing `bmu_influx_flush();` line, before the closing `}` of the `for(;;)` loop. The flush at the end already handles both battery and solar writes.

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -5
```

Expected: `Project build complete`

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/main/main.cpp
git commit -m "feat: solar telemetry via MQTT + InfluxDB"
```

---

### Task 3: VRM component — Kconfig + header + CMake

**Files:**
- Create: `firmware-idf/components/bmu_vrm/Kconfig`
- Create: `firmware-idf/components/bmu_vrm/include/bmu_vrm.h`
- Create: `firmware-idf/components/bmu_vrm/CMakeLists.txt`

- [ ] **Step 1: Create Kconfig**

Create `firmware-idf/components/bmu_vrm/Kconfig`:

```
menu "BMU VRM (Victron Remote Monitoring)"
    config BMU_VRM_ENABLED
        bool "Enable VRM publication"
        default n

    config BMU_VRM_PORTAL_ID
        string "VRM Portal ID"
        default "70b3d549969af37b"
        depends on BMU_VRM_ENABLED

    config BMU_VRM_USE_TLS
        bool "Use TLS (port 8883)"
        default y
        depends on BMU_VRM_ENABLED

    config BMU_VRM_PUBLISH_INTERVAL_S
        int "Publish interval (seconds)"
        default 30
        range 10 300
        depends on BMU_VRM_ENABLED

    config BMU_VRM_SOC_V_MIN
        int "SOC 0% voltage (mV)"
        default 24000
        depends on BMU_VRM_ENABLED

    config BMU_VRM_SOC_V_MAX
        int "SOC 100% voltage (mV)"
        default 28800
        depends on BMU_VRM_ENABLED
endmenu
```

- [ ] **Step 2: Create header**

Create `firmware-idf/components/bmu_vrm/include/bmu_vrm.h`:

```c
#pragma once
#include "esp_err.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Init VRM MQTT client. Starts a FreeRTOS task that publishes
 *        solar charger + battery monitor data to mqtt.victronenergy.com.
 *        No-op if CONFIG_BMU_VRM_ENABLED is not set.
 */
esp_err_t bmu_vrm_init(bmu_protection_ctx_t *prot,
                       bmu_battery_manager_t *mgr,
                       uint8_t nb_ina);

/** Check if VRM MQTT client is connected. */
bool bmu_vrm_is_connected(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Create CMakeLists.txt**

Create `firmware-idf/components/bmu_vrm/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "bmu_vrm.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_protection bmu_config bmu_vedirect esp_timer
    PRIV_REQUIRES mqtt esp-tls bmu_battery_manager
)
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_vrm/
git commit -m "feat(vrm): component scaffold — Kconfig + header"
```

---

### Task 4: VRM implementation — MQTT publish to VRM

**Files:**
- Create: `firmware-idf/components/bmu_vrm/bmu_vrm.cpp`
- Modify: `firmware-idf/sdkconfig.defaults`

- [ ] **Step 1: Implement bmu_vrm.cpp**

Create `firmware-idf/components/bmu_vrm/bmu_vrm.cpp`:

```cpp
/**
 * @file bmu_vrm.cpp
 * @brief Victron VRM cloud publication via MQTT.
 *
 * Publishes solar charger + battery monitor data to mqtt.victronenergy.com.
 * Topic format: N/<portal_id>/<device_class>/<instance>/<path>
 * Value format: {"value": <number_or_string>}
 * Keepalive: R/<portal_id>/keepalive every publish interval.
 */
#include "sdkconfig.h"

#if CONFIG_BMU_VRM_ENABLED

#include "bmu_vrm.h"
#include "bmu_vedirect.h"
#include "bmu_config.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"

#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "VRM";

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static bmu_protection_ctx_t *s_prot = NULL;
static bmu_battery_manager_t *s_mgr = NULL;
static uint8_t s_nb_ina = 0;

/* ── Helpers ──────────────────────────────────────────────────────── */

static void vrm_publish(const char *path, const char *value_json)
{
    if (!s_connected || s_client == NULL) return;
    char topic[128];
    snprintf(topic, sizeof(topic), "N/%s/%s",
             CONFIG_BMU_VRM_PORTAL_ID, path);
    esp_mqtt_client_publish(s_client, topic, value_json, 0, 0, 0);
}

static void vrm_publish_float(const char *path, float val)
{
    char json[32];
    snprintf(json, sizeof(json), "{\"value\":%.2f}", val);
    vrm_publish(path, json);
}

static void vrm_publish_int(const char *path, int val)
{
    char json[32];
    snprintf(json, sizeof(json), "{\"value\":%d}", val);
    vrm_publish(path, json);
}

static void vrm_publish_str(const char *path, const char *val)
{
    char json[64];
    snprintf(json, sizeof(json), "{\"value\":\"%s\"}", val);
    vrm_publish(path, json);
}

/* ── SOC estimation ───────────────────────────────────────────────── */

static float estimate_soc(float avg_voltage_v)
{
    float v_min = (float)CONFIG_BMU_VRM_SOC_V_MIN / 1000.0f;
    float v_max = (float)CONFIG_BMU_VRM_SOC_V_MAX / 1000.0f;
    float soc = (avg_voltage_v - v_min) / (v_max - v_min) * 100.0f;
    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;
    return soc;
}

/* ── Publish solar charger data ───────────────────────────────────── */

static void publish_solar(void)
{
    if (!bmu_vedirect_is_connected()) return;
    const bmu_vedirect_data_t *d = bmu_vedirect_get_data();
    if (!d || !d->valid) return;

    vrm_publish_float("solarcharger/0/Pv/V", d->panel_voltage_v);
    vrm_publish_int("solarcharger/0/Pv/P", d->panel_power_w);
    vrm_publish_float("solarcharger/0/Dc/0/Voltage", d->battery_voltage_v);
    vrm_publish_float("solarcharger/0/Dc/0/Current", d->battery_current_a);
    vrm_publish_int("solarcharger/0/State", d->charge_state);
    vrm_publish_float("solarcharger/0/Yield/User",
                      d->yield_today_wh / 1000.0f);
    vrm_publish_int("solarcharger/0/ErrorCode", d->error_code);
    vrm_publish_str("solarcharger/0/ProductId", d->product_id);
    vrm_publish_str("solarcharger/0/Serial", d->serial);
}

/* ── Publish battery monitor data ─────────────────────────────────── */

static void publish_battery(void)
{
    if (s_nb_ina == 0) return;

    int nb = s_nb_ina > 16 ? 16 : s_nb_ina;
    float sum_v = 0;
    float sum_i = 0;
    float sum_ah_d = 0;
    int n_active = 0;

    for (int i = 0; i < nb; i++) {
        float v_mv = bmu_protection_get_voltage(s_prot, i);
        float v = v_mv / 1000.0f;
        bmu_battery_state_t st = bmu_protection_get_state(s_prot, i);
        if (st == BMU_STATE_CONNECTED && v > 1.0f) {
            sum_v += v;
            n_active++;
        }
        /* Current from battery manager Ah delta approximation */
        sum_ah_d += bmu_battery_manager_get_ah_discharge(s_mgr, i);
    }

    float avg_v = n_active > 0 ? sum_v / n_active : 0;
    float soc = estimate_soc(avg_v);

    vrm_publish_float("battery/0/Dc/0/Voltage", avg_v);
    vrm_publish_float("battery/0/Dc/0/Current", sum_i);
    vrm_publish_float("battery/0/Soc", soc);
    vrm_publish_float("battery/0/ConsumedAmphours", sum_ah_d);
}

/* ── Keepalive ────────────────────────────────────────────────────── */

static void publish_keepalive(void)
{
    if (!s_connected || s_client == NULL) return;
    char topic[64];
    snprintf(topic, sizeof(topic), "R/%s/keepalive",
             CONFIG_BMU_VRM_PORTAL_ID);
    esp_mqtt_client_publish(s_client, topic, "", 0, 0, 0);
}

/* ── MQTT event handler ───────────────────────────────────────────── */

static void vrm_mqtt_event_handler(void *arg, esp_event_base_t base,
                                   int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;
    switch (ev->event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "VRM MQTT connected");
            /* Register system serial */
            vrm_publish_str("system/0/Serial", CONFIG_BMU_VRM_PORTAL_ID);
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "VRM MQTT disconnected");
            break;
        default:
            break;
    }
}

/* ── Task ─────────────────────────────────────────────────────────── */

static void vrm_task(void *pv)
{
    (void)pv;
    const TickType_t period = pdMS_TO_TICKS(
        CONFIG_BMU_VRM_PUBLISH_INTERVAL_S * 1000);

    ESP_LOGI(TAG, "VRM task started — interval %ds, portal %s",
             CONFIG_BMU_VRM_PUBLISH_INTERVAL_S,
             CONFIG_BMU_VRM_PORTAL_ID);

    for (;;) {
        vTaskDelay(period);
        if (!s_connected) continue;

        publish_keepalive();
        publish_solar();
        publish_battery();
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

esp_err_t bmu_vrm_init(bmu_protection_ctx_t *prot,
                       bmu_battery_manager_t *mgr,
                       uint8_t nb_ina)
{
    s_prot = prot;
    s_mgr = mgr;
    s_nb_ina = nb_ina;

    const char *broker = CONFIG_BMU_VRM_USE_TLS
        ? "mqtts://mqtt.victronenergy.com:8883"
        : "mqtt://mqtt.victronenergy.com:1883";

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri = broker;
    cfg.credentials.client_id = CONFIG_BMU_VRM_PORTAL_ID;
    cfg.session.keepalive = CONFIG_BMU_VRM_PUBLISH_INTERVAL_S;
    cfg.network.reconnect_timeout_ms = 10000;
    cfg.buffer.size = 1024;

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "VRM MQTT client init failed");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY,
                                   vrm_mqtt_event_handler, NULL);
    esp_err_t ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "VRM MQTT start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    xTaskCreate(vrm_task, "vrm", 4096, NULL, 2, NULL);
    ESP_LOGI(TAG, "VRM init OK — broker %s", broker);
    return ESP_OK;
}

bool bmu_vrm_is_connected(void)
{
    return s_connected;
}

#else /* !CONFIG_BMU_VRM_ENABLED */

#include "bmu_vrm.h"
esp_err_t bmu_vrm_init(bmu_protection_ctx_t *p, bmu_battery_manager_t *m, uint8_t n)
{
    (void)p; (void)m; (void)n;
    return ESP_OK;
}
bool bmu_vrm_is_connected(void) { return false; }

#endif
```

- [ ] **Step 2: Add VRM enable to sdkconfig.defaults**

Append to `firmware-idf/sdkconfig.defaults`:

```
# ── VRM (Victron Remote Monitoring) ──────────────────────────────────
CONFIG_BMU_VRM_ENABLED=y
CONFIG_BMU_VRM_PORTAL_ID="70b3d549969af37b"
CONFIG_BMU_VRM_USE_TLS=y
CONFIG_BMU_VRM_PUBLISH_INTERVAL_S=30
CONFIG_BMU_VRM_SOC_V_MIN=24000
CONFIG_BMU_VRM_SOC_V_MAX=28800
```

- [ ] **Step 3: Build**

```bash
rm -rf build/sdkconfig && idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_vrm/ firmware-idf/sdkconfig.defaults
git commit -m "feat(vrm): MQTT client for Victron VRM cloud"
```

---

### Task 5: VRM init in main.cpp

**Files:**
- Modify: `firmware-idf/main/main.cpp`

- [ ] **Step 1: Add VRM include and init**

In `firmware-idf/main/main.cpp`, add include at the top with the others:

```cpp
#include "bmu_vrm.h"
```

After the cloud telemetry task creation block (after `xTaskCreate(cloud_telemetry_task, ...)`), add:

```cpp
        /* VRM — publish to Victron cloud */
        bmu_vrm_init(&prot, &mgr, nb_ina);
```

This goes inside the `if (bmu_wifi_is_connected())` block, after InfluxDB init and cloud task creation.

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/main/main.cpp
git commit -m "feat(vrm): init VRM client at boot"
```

---

### Task 6: BLE Victron component — scaffold

**Files:**
- Create: `firmware-idf/components/bmu_ble_victron/Kconfig`
- Create: `firmware-idf/components/bmu_ble_victron/include/bmu_ble_victron.h`
- Create: `firmware-idf/components/bmu_ble_victron/CMakeLists.txt`

- [ ] **Step 1: Create Kconfig**

Create `firmware-idf/components/bmu_ble_victron/Kconfig`:

```
menu "BMU BLE Victron (Instant Readout)"
    config BMU_VICTRON_BLE_ENABLED
        bool "Enable Victron BLE advertising"
        default n
        depends on BT_NIMBLE_ENABLED

    config BMU_VICTRON_BLE_KEY
        string "AES-128 encryption key (32 hex chars)"
        default "00112233445566778899aabbccddeeff"
        depends on BMU_VICTRON_BLE_ENABLED

    config BMU_VICTRON_ADV_INTERVAL_MS
        int "Advertising slot rotation interval (ms)"
        default 500
        range 200 2000
        depends on BMU_VICTRON_BLE_ENABLED
endmenu
```

- [ ] **Step 2: Create header**

Create `firmware-idf/components/bmu_ble_victron/include/bmu_ble_victron.h`:

```c
#pragma once
#include "esp_err.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start Victron-format BLE advertising (Instant Readout).
 *
 * Alternates advertising data between:
 * - KXKM-BMU (existing connectable advertising)
 * - Victron Battery Monitor (record type 0x02)
 * - Victron Solar Charger (record type 0x01)
 *
 * Uses Victron Company ID 0x02E1, AES-CTR encrypted payloads.
 * No-op if CONFIG_BMU_VICTRON_BLE_ENABLED is not set.
 */
esp_err_t bmu_ble_victron_init(bmu_protection_ctx_t *prot,
                               bmu_battery_manager_t *mgr,
                               uint8_t nb_ina);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Create CMakeLists.txt**

Create `firmware-idf/components/bmu_ble_victron/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "bmu_ble_victron.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bmu_protection bmu_config bmu_vedirect bt esp_timer
    PRIV_REQUIRES bmu_battery_manager mbedtls
)
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_ble_victron/
git commit -m "feat(ble-victron): component scaffold"
```

---

### Task 7: BLE Victron implementation — Instant Readout

**Files:**
- Create: `firmware-idf/components/bmu_ble_victron/bmu_ble_victron.cpp`
- Modify: `firmware-idf/sdkconfig.defaults`

- [ ] **Step 1: Implement bmu_ble_victron.cpp**

Create `firmware-idf/components/bmu_ble_victron/bmu_ble_victron.cpp`:

```cpp
/**
 * @file bmu_ble_victron.cpp
 * @brief Victron BLE Instant Readout — advertising encrypted battery/solar data.
 *
 * Victron Connect reads Manufacturer Specific Data (Company ID 0x02E1) from
 * BLE advertising packets. Data is AES-CTR-128 encrypted with a user-shared key.
 *
 * References:
 * - https://github.com/keshavdv/victron-ble (reverse-engineered protocol)
 * - Victron Company ID: 0x02E1
 */
#include "sdkconfig.h"

#if CONFIG_BMU_VICTRON_BLE_ENABLED

#include "bmu_ble_victron.h"
#include "bmu_vedirect.h"
#include "bmu_config.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "mbedtls/aes.h"

#include <cstring>
#include <cstdio>

static const char *TAG = "BLE_VIC";

/* Victron BLE constants */
#define VICTRON_COMPANY_ID      0x02E1
#define VICTRON_RECORD_SOLAR    0x01
#define VICTRON_RECORD_BATTERY  0x02

/* State */
static bmu_protection_ctx_t *s_prot = NULL;
static bmu_battery_manager_t *s_mgr = NULL;
static uint8_t s_nb_ina = 0;
static uint16_t s_adv_counter = 0;
static uint8_t s_aes_key[16] = {};
static esp_timer_handle_t s_adv_timer = NULL;

/* Advertising slot: 0=KXKM, 1=Victron Battery, 2=Victron Solar */
static int s_adv_slot = 0;

/* ── AES key parse ────────────────────────────────────────────────── */

static void parse_hex_key(const char *hex, uint8_t *out)
{
    for (int i = 0; i < 16; i++) {
        unsigned byte = 0;
        sscanf(hex + i * 2, "%02x", &byte);
        out[i] = (uint8_t)byte;
    }
}

/* ── AES-CTR encrypt ──────────────────────────────────────────────── */

static void encrypt_payload(const uint8_t *plain, uint8_t *cipher,
                            size_t len, uint16_t counter)
{
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, s_aes_key, 128);

    uint8_t nonce[16] = {};
    nonce[0] = (uint8_t)(counter & 0xFF);
    nonce[1] = (uint8_t)(counter >> 8);

    size_t nc_off = 0;
    uint8_t stream[16] = {};
    mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce, stream, plain, cipher);
    mbedtls_aes_free(&aes);
}

/* ── Build Battery Monitor advertising data ───────────────────────── */

static int build_battery_adv(uint8_t *buf, size_t buf_len)
{
    if (s_nb_ina == 0) return 0;

    /* Compute aggregates */
    int nb = s_nb_ina > 16 ? 16 : s_nb_ina;
    float sum_v = 0, sum_ah_c = 0, sum_ah_d = 0;
    int n_active = 0;
    for (int i = 0; i < nb; i++) {
        float v_mv = bmu_protection_get_voltage(s_prot, i);
        float v = v_mv / 1000.0f;
        bmu_battery_state_t st = bmu_protection_get_state(s_prot, i);
        if (st == BMU_STATE_CONNECTED && v > 1.0f) {
            sum_v += v;
            n_active++;
        }
        sum_ah_c += bmu_battery_manager_get_ah_charge(s_mgr, i);
        sum_ah_d += bmu_battery_manager_get_ah_discharge(s_mgr, i);
    }
    float avg_v = n_active > 0 ? sum_v / n_active : 0;
    float soc = (avg_v - (float)CONFIG_BMU_VRM_SOC_V_MIN / 1000.0f)
              / ((float)CONFIG_BMU_VRM_SOC_V_MAX / 1000.0f
               - (float)CONFIG_BMU_VRM_SOC_V_MIN / 1000.0f) * 100.0f;
    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;

    /* Pack plaintext: remaining_ah(u16), voltage(u16), current(i16), soc(u16), consumed_ah(u16) = 10 bytes */
    uint8_t plain[10];
    uint16_t remaining_ah = (uint16_t)(sum_ah_c * 10.0f);     /* 0.1 Ah */
    uint16_t voltage      = (uint16_t)(avg_v * 100.0f);       /* 0.01 V */
    int16_t  current      = 0;                                  /* 0.1 A — todo: sum currents */
    uint16_t soc_u16      = (uint16_t)(soc * 10.0f);          /* 0.1 % */
    uint16_t consumed_ah  = (uint16_t)(sum_ah_d * 10.0f);     /* 0.1 Ah */

    memcpy(plain + 0, &remaining_ah, 2);
    memcpy(plain + 2, &voltage, 2);
    memcpy(plain + 4, &current, 2);
    memcpy(plain + 6, &soc_u16, 2);
    memcpy(plain + 8, &consumed_ah, 2);

    /* Encrypt */
    uint8_t cipher[10];
    encrypt_payload(plain, cipher, 10, s_adv_counter);

    /* Build manufacturer specific data:
     * [company_id_lo, company_id_hi, record_type, counter_lo, counter_hi, encrypted...] */
    if (buf_len < 15) return 0;
    buf[0] = (uint8_t)(VICTRON_COMPANY_ID & 0xFF);
    buf[1] = (uint8_t)(VICTRON_COMPANY_ID >> 8);
    buf[2] = VICTRON_RECORD_BATTERY;
    buf[3] = (uint8_t)(s_adv_counter & 0xFF);
    buf[4] = (uint8_t)(s_adv_counter >> 8);
    memcpy(buf + 5, cipher, 10);
    return 15;
}

/* ── Build Solar Charger advertising data ─────────────────────────── */

static int build_solar_adv(uint8_t *buf, size_t buf_len)
{
    if (!bmu_vedirect_is_connected()) return 0;
    const bmu_vedirect_data_t *d = bmu_vedirect_get_data();
    if (!d || !d->valid) return 0;

    /* Pack plaintext: state(u8), error(u8), yield_today(u16 0.01kWh),
     * pv_power(u16 W), bat_current(i16 0.1A), bat_voltage(u16 0.01V) = 10 bytes */
    uint8_t plain[10];
    plain[0] = d->charge_state;
    plain[1] = d->error_code;
    uint16_t yield = (uint16_t)(d->yield_today_wh / 10); /* Wh → 0.01 kWh */
    uint16_t ppv   = d->panel_power_w;
    int16_t  ibat  = (int16_t)(d->battery_current_a * 10.0f);
    uint16_t vbat  = (uint16_t)(d->battery_voltage_v * 100.0f);

    memcpy(plain + 2, &yield, 2);
    memcpy(plain + 4, &ppv, 2);
    memcpy(plain + 6, &ibat, 2);
    memcpy(plain + 8, &vbat, 2);

    /* Encrypt */
    uint8_t cipher[10];
    encrypt_payload(plain, cipher, 10, s_adv_counter);

    if (buf_len < 15) return 0;
    buf[0] = (uint8_t)(VICTRON_COMPANY_ID & 0xFF);
    buf[1] = (uint8_t)(VICTRON_COMPANY_ID >> 8);
    buf[2] = VICTRON_RECORD_SOLAR;
    buf[3] = (uint8_t)(s_adv_counter & 0xFF);
    buf[4] = (uint8_t)(s_adv_counter >> 8);
    memcpy(buf + 5, cipher, 10);
    return 15;
}

/* ── Advertising rotation timer ───────────────────────────────────── */

static void adv_rotate_cb(void *arg)
{
    (void)arg;
    s_adv_slot = (s_adv_slot + 1) % 3;

    if (s_adv_slot == 0) {
        /* Slot 0: KXKM-BMU advertising — let the existing BLE handle it.
         * We stop our custom adv data and let the normal advertising resume. */
        return;
    }

    uint8_t mfr_data[16] = {};
    int mfr_len = 0;

    if (s_adv_slot == 1) {
        mfr_len = build_battery_adv(mfr_data, sizeof(mfr_data));
    } else {
        mfr_len = build_solar_adv(mfr_data, sizeof(mfr_data));
    }

    if (mfr_len == 0) return;

    s_adv_counter++;

    /* Set advertising data with Victron manufacturer specific data */
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.mfg_data = mfr_data;
    fields.mfg_data_len = (uint8_t)mfr_len;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGD(TAG, "adv_set_fields failed: %d", rc);
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

esp_err_t bmu_ble_victron_init(bmu_protection_ctx_t *prot,
                               bmu_battery_manager_t *mgr,
                               uint8_t nb_ina)
{
    s_prot = prot;
    s_mgr = mgr;
    s_nb_ina = nb_ina;

    /* Parse AES key from config */
    const char *hex_key = CONFIG_BMU_VICTRON_BLE_KEY;
    if (strlen(hex_key) != 32) {
        ESP_LOGE(TAG, "Invalid BLE key length: %d (expected 32 hex chars)",
                 (int)strlen(hex_key));
        return ESP_ERR_INVALID_ARG;
    }
    parse_hex_key(hex_key, s_aes_key);

    /* Start advertising rotation timer */
    const esp_timer_create_args_t timer_args = {
        .callback = adv_rotate_cb,
        .arg = NULL,
        .name = "vic_adv",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_adv_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(
        s_adv_timer,
        CONFIG_BMU_VICTRON_ADV_INTERVAL_MS * 1000ULL));

    ESP_LOGI(TAG, "Victron BLE Instant Readout started — interval %dms",
             CONFIG_BMU_VICTRON_ADV_INTERVAL_MS);
    return ESP_OK;
}

#else /* !CONFIG_BMU_VICTRON_BLE_ENABLED */

#include "bmu_ble_victron.h"
esp_err_t bmu_ble_victron_init(bmu_protection_ctx_t *p,
                               bmu_battery_manager_t *m, uint8_t n)
{
    (void)p; (void)m; (void)n;
    return ESP_OK;
}

#endif
```

- [ ] **Step 2: Add BLE Victron enable to sdkconfig.defaults**

Append to `firmware-idf/sdkconfig.defaults`:

```
# ── BLE Victron Instant Readout ──────────────────────────────────────
CONFIG_BMU_VICTRON_BLE_ENABLED=y
CONFIG_BMU_VICTRON_BLE_KEY="00112233445566778899aabbccddeeff"
CONFIG_BMU_VICTRON_ADV_INTERVAL_MS=500
```

- [ ] **Step 3: Build**

```bash
rm -rf build/sdkconfig && idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_ble_victron/ firmware-idf/sdkconfig.defaults
git commit -m "feat(ble-victron): Instant Readout advertising"
```

---

### Task 8: BLE Victron init in main.cpp

**Files:**
- Modify: `firmware-idf/main/main.cpp`

- [ ] **Step 1: Add BLE Victron include and init**

In `firmware-idf/main/main.cpp`, add include:

```cpp
#include "bmu_ble_victron.h"
```

After the existing BLE init block (`#ifdef CONFIG_BMU_BLE_ENABLED ... #endif`), add:

```cpp
#ifdef CONFIG_BMU_VICTRON_BLE_ENABLED
    bmu_ble_victron_init(&prot, &mgr, nb_ina);
#endif
```

Note: this should be placed after `bmu_wifi_start()` and after the existing BLE init, but before the I2C section. The nb_ina will be 0 at this point (I2C scan hasn't happened yet), but the Victron BLE reads from protection context which gets updated later. We need to move the init to after the topology scan instead.

Actually, place it after the display context update (`disp_ctx.nb_ina = nb_ina;`, line ~231):

```cpp
    /* Update display context avec nb_ina reel */
    disp_ctx.nb_ina = nb_ina;

#ifdef CONFIG_BMU_VICTRON_BLE_ENABLED
    bmu_ble_victron_init(&prot, &mgr, nb_ina);
#endif
```

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/main/main.cpp
git commit -m "feat(ble-victron): init at boot after I2C scan"
```

---

### Task 9: Config NVS — VRM + BLE Victron settings

**Files:**
- Modify: `firmware-idf/components/bmu_config/include/bmu_config.h`
- Modify: `firmware-idf/components/bmu_config/bmu_config.cpp`

- [ ] **Step 1: Add declarations to bmu_config.h**

In `firmware-idf/components/bmu_config/include/bmu_config.h`, before the `#ifdef __cplusplus` closing, add:

```c
/* ── VRM settings ─────────────────────────────────────────────────── */
#define BMU_CONFIG_VRM_ID_MAX   20

esp_err_t   bmu_config_set_vrm_portal_id(const char *id);
const char *bmu_config_get_vrm_portal_id(void);
esp_err_t   bmu_config_set_vrm_enabled(bool enabled);
bool        bmu_config_get_vrm_enabled(void);

/* ── BLE Victron settings ─────────────────────────────────────────── */
#define BMU_CONFIG_BLE_KEY_MAX  33  /* 32 hex chars + NUL */

esp_err_t   bmu_config_set_victron_ble_key(const char *hex_key);
const char *bmu_config_get_victron_ble_key(void);
esp_err_t   bmu_config_set_victron_ble_enabled(bool enabled);
bool        bmu_config_get_victron_ble_enabled(void);
```

- [ ] **Step 2: Add implementation to bmu_config.cpp**

In `firmware-idf/components/bmu_config/bmu_config.cpp`, add NVS keys after the existing defines:

```cpp
#define NVS_KEY_VRM_ID     "vrm_id"
#define NVS_KEY_VRM_EN     "vrm_en"
#define NVS_KEY_VIC_KEY    "vrm_ble_key"
#define NVS_KEY_VIC_EN     "vrm_ble_en"
```

Add cache variables after existing ones:

```cpp
static char     s_vrm_portal_id[BMU_CONFIG_VRM_ID_MAX];
static bool     s_vrm_enabled = false;
static char     s_victron_ble_key[BMU_CONFIG_BLE_KEY_MAX];
static bool     s_victron_ble_enabled = false;
```

In `bmu_config_load()`, after loading mqtt, add:

```cpp
    /* VRM + BLE Victron defaults */
    strncpy(s_vrm_portal_id, CONFIG_BMU_VRM_PORTAL_ID, sizeof(s_vrm_portal_id) - 1);
    s_vrm_enabled = true;  /* enabled by default if Kconfig says so */
    strncpy(s_victron_ble_key, CONFIG_BMU_VICTRON_BLE_KEY, sizeof(s_victron_ble_key) - 1);
    s_victron_ble_enabled = true;
```

In the NVS read section, after loading mqtt_uri, add:

```cpp
        load_str(h, NVS_KEY_VRM_ID, s_vrm_portal_id, sizeof(s_vrm_portal_id), CONFIG_BMU_VRM_PORTAL_ID);
        load_str(h, NVS_KEY_VIC_KEY, s_victron_ble_key, sizeof(s_victron_ble_key), CONFIG_BMU_VICTRON_BLE_KEY);
        /* Bool flags stored as u16 (0/1) */
        s_vrm_enabled = load_u16(h, NVS_KEY_VRM_EN, 1) != 0;
        s_victron_ble_enabled = load_u16(h, NVS_KEY_VIC_EN, 1) != 0;
```

Add the API implementations at the end of the file:

```cpp
/* ── API: VRM ─────────────────────────────────────────────────────── */

esp_err_t bmu_config_set_vrm_portal_id(const char *id)
{
    if (id == nullptr || id[0] == '\0') return ESP_ERR_INVALID_ARG;
    esp_err_t ret = save_str(NVS_KEY_VRM_ID, id);
    if (ret == ESP_OK) {
        strncpy(s_vrm_portal_id, id, sizeof(s_vrm_portal_id) - 1);
        s_vrm_portal_id[sizeof(s_vrm_portal_id) - 1] = '\0';
    }
    return ret;
}

const char *bmu_config_get_vrm_portal_id(void) { return s_vrm_portal_id; }

esp_err_t bmu_config_set_vrm_enabled(bool enabled)
{
    esp_err_t ret = save_u16(NVS_KEY_VRM_EN, enabled ? 1 : 0);
    if (ret == ESP_OK) s_vrm_enabled = enabled;
    return ret;
}

bool bmu_config_get_vrm_enabled(void) { return s_vrm_enabled; }

/* ── API: BLE Victron ─────────────────────────────────────────────── */

esp_err_t bmu_config_set_victron_ble_key(const char *hex_key)
{
    if (hex_key == nullptr || strlen(hex_key) != 32) return ESP_ERR_INVALID_ARG;
    esp_err_t ret = save_str(NVS_KEY_VIC_KEY, hex_key);
    if (ret == ESP_OK) {
        strncpy(s_victron_ble_key, hex_key, sizeof(s_victron_ble_key) - 1);
        s_victron_ble_key[sizeof(s_victron_ble_key) - 1] = '\0';
    }
    return ret;
}

const char *bmu_config_get_victron_ble_key(void) { return s_victron_ble_key; }

esp_err_t bmu_config_set_victron_ble_enabled(bool enabled)
{
    esp_err_t ret = save_u16(NVS_KEY_VIC_EN, enabled ? 1 : 0);
    if (ret == ESP_OK) s_victron_ble_enabled = enabled;
    return ret;
}

bool bmu_config_get_victron_ble_enabled(void) { return s_victron_ble_enabled; }
```

- [ ] **Step 3: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_config/
git commit -m "feat(config): VRM + BLE Victron NVS settings"
```

---

### Task 10: Config screen — VICTRON section

**Files:**
- Modify: `firmware-idf/components/bmu_display/bmu_ui_config.cpp`

- [ ] **Step 1: Add VICTRON section to config screen**

In `firmware-idf/components/bmu_display/bmu_ui_config.cpp`, in `bmu_ui_config_create()`, add a new section before the Save button. Add static widgets at the top of the file:

```cpp
static lv_obj_t *s_vrm_id_ta = NULL;
static lv_obj_t *s_vrm_sw = NULL;
static lv_obj_t *s_vic_key_ta = NULL;
static lv_obj_t *s_vic_sw = NULL;
```

Add the section creation before the Save button code:

```cpp
    /* Victron */
    section_label(cont, "VICTRON");

    /* VRM Portal ID */
    s_vrm_id_ta = create_textarea(cont, bmu_config_get_vrm_portal_id(), false, 19);

    /* VRM enabled toggle */
    lv_obj_t *vrm_row = lv_obj_create(cont);
    lv_obj_set_size(vrm_row, lv_pct(100), 28);
    lv_obj_set_flex_flow(vrm_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vrm_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(vrm_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vrm_row, 0, 0);
    lv_obj_set_style_pad_all(vrm_row, 0, 0);
    lv_obj_t *vrm_lbl = lv_label_create(vrm_row);
    lv_label_set_text(vrm_lbl, "VRM");
    lv_obj_set_style_text_color(vrm_lbl, UI_COLOR_TEXT_SEC, 0);
    s_vrm_sw = lv_switch_create(vrm_row);
    if (bmu_config_get_vrm_enabled()) lv_obj_add_state(s_vrm_sw, LV_STATE_CHECKED);

    /* BLE Key (hex) */
    s_vic_key_ta = create_textarea(cont, bmu_config_get_victron_ble_key(), false, 32);

    /* BLE Victron enabled toggle */
    lv_obj_t *vic_row = lv_obj_create(cont);
    lv_obj_set_size(vic_row, lv_pct(100), 28);
    lv_obj_set_flex_flow(vic_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vic_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(vic_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vic_row, 0, 0);
    lv_obj_set_style_pad_all(vic_row, 0, 0);
    lv_obj_t *vic_lbl = lv_label_create(vic_row);
    lv_label_set_text(vic_lbl, "BLE Victron");
    lv_obj_set_style_text_color(vic_lbl, UI_COLOR_TEXT_SEC, 0);
    s_vic_sw = lv_switch_create(vic_row);
    if (bmu_config_get_victron_ble_enabled()) lv_obj_add_state(s_vic_sw, LV_STATE_CHECKED);
```

In the save button callback, add:

```cpp
        /* Victron */
        bmu_config_set_vrm_portal_id(lv_textarea_get_text(s_vrm_id_ta));
        bmu_config_set_vrm_enabled(lv_obj_has_state(s_vrm_sw, LV_STATE_CHECKED));
        bmu_config_set_victron_ble_key(lv_textarea_get_text(s_vic_key_ta));
        bmu_config_set_victron_ble_enabled(lv_obj_has_state(s_vic_sw, LV_STATE_CHECKED));
```

- [ ] **Step 2: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 3: Commit**

```bash
git add firmware-idf/components/bmu_display/bmu_ui_config.cpp
git commit -m "feat(ui): Victron section in config screen"
```

---

### Task 11: Integration build + flash

**Files:** None (verification only)

- [ ] **Step 1: Clean build**

```bash
cd firmware-idf && rm -rf build
export IDF_PATH=$HOME/esp/esp-idf && . $IDF_PATH/export.sh
idf.py set-target esp32s3 && idf.py build 2>&1 | tail -10
```

Expected: `Project build complete`, binary < 2 MB

- [ ] **Step 2: Flash**

```bash
idf.py -p /dev/cu.usbmodem* flash
```

- [ ] **Step 3: Verify on hardware**

Check:
- Serial monitor: VE.Direct parser active, frames received from SmartSolar
- Serial monitor: VRM MQTT connected to mqtt.victronenergy.com
- Serial monitor: Victron BLE advertising rotating
- SYS screen: Solar section shows SmartSolar data
- CONFIG screen: VICTRON section with Portal ID, BLE Key, toggles
- Victron VRM portal: data appears (solar charger + battery monitor)
- Victron Connect app: scan for BLE Instant Readout devices — BMU visible

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "feat: Victron integration complete — VE.Direct + VRM + BLE"
```
