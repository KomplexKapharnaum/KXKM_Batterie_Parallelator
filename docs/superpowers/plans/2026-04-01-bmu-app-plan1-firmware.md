# BMU App — Plan 1: Firmware Changes

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add WiFi Config/Status BLE characteristics, /api/system and /api/solar HTTP routes, and WiFi RSSI public API to the BMU firmware, enabling the smartphone app to configure WiFi via BLE and read system/solar data via HTTP.

**Architecture:** Extend existing BLE Control Service (0x0003) with 2 new GATT characteristics following the established pattern. Add 2 GET routes to bmu_web following existing handler pattern. Add bmu_wifi_get_rssi() to WiFi module.

**Tech Stack:** ESP-IDF v5.4, NimBLE, esp_http_server, cJSON

**Spec:** `docs/superpowers/specs/2026-04-01-smartphone-app-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `firmware-idf/components/bmu_wifi/include/bmu_wifi.h` | Modify | Add `bmu_wifi_get_rssi()` declaration |
| `firmware-idf/components/bmu_wifi/bmu_wifi.cpp` | Modify | Implement `bmu_wifi_get_rssi()` |
| `firmware-idf/components/bmu_ble/bmu_ble_control_svc.cpp` | Modify | Add WiFi Config (0x0034) + WiFi Status (0x0035) characteristics |
| `firmware-idf/components/bmu_ble/include/bmu_ble_internal.h` | Modify | Add WiFi notify start/stop declarations |
| `firmware-idf/components/bmu_web/bmu_web.cpp` | Modify | Add `/api/system` and `/api/solar` GET routes |

---

### Task 1: Add bmu_wifi_get_rssi()

**Files:**
- Modify: `firmware-idf/components/bmu_wifi/include/bmu_wifi.h`
- Modify: `firmware-idf/components/bmu_wifi/bmu_wifi.cpp`

- [ ] **Step 1: Add declaration to bmu_wifi.h**

Add after the `bmu_wifi_get_ip` declaration:

```c
/**
 * @brief Get current WiFi RSSI (signal strength).
 * @param[out] rssi RSSI in dBm (negative value, e.g. -65).
 * @return ESP_OK if connected, ESP_ERR_WIFI_NOT_CONNECT if not.
 */
esp_err_t bmu_wifi_get_rssi(int8_t *rssi);
```

- [ ] **Step 2: Implement in bmu_wifi.cpp**

Add at the end of the file, before the closing `#endif` or end of file:

```cpp
esp_err_t bmu_wifi_get_rssi(int8_t *rssi)
{
    if (!bmu_wifi_is_connected() || rssi == nullptr) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        *rssi = ap_info.rssi;
    }
    return ret;
}
```

- [ ] **Step 3: Build to verify compilation**

Run:
```bash
cd firmware-idf && idf.py build 2>&1 | tail -5
```
Expected: `Project build complete`

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_wifi/include/bmu_wifi.h \
        firmware-idf/components/bmu_wifi/bmu_wifi.cpp
git commit -m "feat(wifi): add bmu_wifi_get_rssi() for app WiFi status"
```

---

### Task 2: Add /api/system HTTP route

**Files:**
- Modify: `firmware-idf/components/bmu_web/bmu_web.cpp`

- [ ] **Step 1: Add handler function**

Add the handler function before the `bmu_web_start()` function (before the route registration block). This handler returns system info as JSON:

```cpp
static esp_err_t handler_api_system(httpd_req_t *req)
{
    bmu_web_ctx_t *ctx = (bmu_web_ctx_t *)req->user_ctx;
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    /* Firmware version */
    cJSON_AddStringToObject(root, "fw_version", CONFIG_APP_PROJECT_VER);

    /* Uptime */
    cJSON_AddNumberToObject(root, "uptime_s",
                            (double)(esp_timer_get_time() / 1000000LL));

    /* Heap */
    cJSON_AddNumberToObject(root, "heap_free",
                            (double)esp_get_free_heap_size());

    /* Topology */
    cJSON_AddNumberToObject(root, "nb_ina", ctx->prot->nb_ina);
    cJSON_AddNumberToObject(root, "nb_tca", ctx->prot->nb_tca);
    cJSON_AddBoolToObject(root, "topology_valid",
                          (ctx->prot->nb_tca * 4 == ctx->prot->nb_ina)
                          && ctx->prot->nb_ina > 0);

    /* WiFi */
    cJSON_AddBoolToObject(root, "wifi_connected", bmu_wifi_is_connected());
    char ip[16] = {};
    bmu_wifi_get_ip(ip, sizeof(ip));
    cJSON_AddStringToObject(root, "wifi_ip", ip);
    int8_t rssi = 0;
    if (bmu_wifi_get_rssi(&rssi) == ESP_OK) {
        cJSON_AddNumberToObject(root, "wifi_rssi", rssi);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON print failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}
```

- [ ] **Step 2: Register the route**

In `bmu_web_start()`, add route registration after the existing `/api/log` route and before the WebSocket route:

```cpp
    const httpd_uri_t uri_system = {
        .uri      = "/api/system",
        .method   = HTTP_GET,
        .handler  = handler_api_system,
        .user_ctx = ctx
    };
    httpd_register_uri_handler(s_server, &uri_system);
```

- [ ] **Step 3: Add missing includes if needed**

Ensure these are included at the top of bmu_web.cpp (most already are):

```cpp
#include "esp_timer.h"
#include "esp_app_desc.h"
```

- [ ] **Step 4: Build to verify**

Run:
```bash
cd firmware-idf && idf.py build 2>&1 | tail -5
```
Expected: `Project build complete`

- [ ] **Step 5: Commit**

```bash
git add firmware-idf/components/bmu_web/bmu_web.cpp
git commit -m "feat(web): add /api/system route for app system info"
```

---

### Task 3: Add /api/solar HTTP route

**Files:**
- Modify: `firmware-idf/components/bmu_web/bmu_web.cpp`

- [ ] **Step 1: Add include for VE.Direct**

At the top of bmu_web.cpp, add:

```cpp
#include "bmu_vedirect.h"
```

- [ ] **Step 2: Add handler function**

Add the handler next to `handler_api_system`:

```cpp
static esp_err_t handler_api_solar(httpd_req_t *req)
{
    const bmu_vedirect_data_t *solar = bmu_vedirect_get_data();
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON alloc failed");
        return ESP_FAIL;
    }

    if (solar != NULL && solar->valid) {
        cJSON_AddNumberToObject(root, "battery_mv",
                                (int)(solar->battery_voltage_v * 1000.0f));
        cJSON_AddNumberToObject(root, "battery_ma",
                                (int)(solar->battery_current_a * 1000.0f));
        cJSON_AddNumberToObject(root, "panel_mv",
                                (int)(solar->panel_voltage_v * 1000.0f));
        cJSON_AddNumberToObject(root, "panel_w", solar->panel_power_w);
        cJSON_AddNumberToObject(root, "charge_state", solar->charge_state);
        cJSON_AddStringToObject(root, "charge_state_name",
                                bmu_vedirect_cs_name(solar->charge_state));
        cJSON_AddNumberToObject(root, "yield_today_wh", solar->yield_today_wh);
        cJSON_AddBoolToObject(root, "valid", true);
    } else {
        cJSON_AddBoolToObject(root, "valid", false);
    }

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON print failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);
    free(json);
    return ESP_OK;
}
```

- [ ] **Step 3: Register the route**

In `bmu_web_start()`, after the `/api/system` registration:

```cpp
    const httpd_uri_t uri_solar = {
        .uri      = "/api/solar",
        .method   = HTTP_GET,
        .handler  = handler_api_solar,
        .user_ctx = ctx
    };
    httpd_register_uri_handler(s_server, &uri_solar);
```

- [ ] **Step 4: Add bmu_vedirect dependency to bmu_web CMakeLists**

Check `firmware-idf/components/bmu_web/CMakeLists.txt`. If `bmu_vedirect` is not in REQUIRES or PRIV_REQUIRES, add it:

```cmake
idf_component_register(
    SRCS "bmu_web.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ... bmu_vedirect
)
```

- [ ] **Step 5: Build to verify**

Run:
```bash
cd firmware-idf && idf.py build 2>&1 | tail -5
```
Expected: `Project build complete`

- [ ] **Step 6: Commit**

```bash
git add firmware-idf/components/bmu_web/bmu_web.cpp \
        firmware-idf/components/bmu_web/CMakeLists.txt
git commit -m "feat(web): add /api/solar route for VE.Direct data"
```

---

### Task 4: Add BLE WiFi Config characteristic (0x0034)

**Files:**
- Modify: `firmware-idf/components/bmu_ble/bmu_ble_control_svc.cpp`

- [ ] **Step 1: Add UUID declaration**

After `s_status_chr_uuid` (around line 52), add:

```c
static ble_uuid128_t s_wifi_cfg_chr_uuid  = BMU_BLE_UUID128_DECLARE(0x34, 0x00);
static ble_uuid128_t s_wifi_sts_chr_uuid  = BMU_BLE_UUID128_DECLARE(0x35, 0x00);
```

- [ ] **Step 2: Extend enum**

After `CTRL_CHR_STATUS` in the `ctrl_chr_id` enum, add:

```c
    CTRL_CHR_WIFI_CONFIG,
    CTRL_CHR_WIFI_STATUS,
```

- [ ] **Step 3: Add WiFi Config write handler**

Add include at the top of the file:

```c
#include "bmu_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
```

Add the WiFi config handler in the access callback switch statement, after the CONFIG case:

```c
    case CTRL_CHR_WIFI_CONFIG: {
        if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;

        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len < 2 || len > 96) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

        uint8_t buf[96] = {};
        os_mbuf_copydata(ctxt->om, 0, len, buf);

        /* SSID = first 32 bytes (null-terminated), password = next 64 bytes */
        char ssid[33] = {};
        char pass[65] = {};
        memcpy(ssid, buf, 32);
        if (len > 32) memcpy(pass, buf + 32, len > 96 ? 64 : len - 32);

        ESP_LOGI(TAG, "BLE WiFi config: SSID='%s'", ssid);

        /* Save to NVS */
        nvs_handle_t nvs;
        if (nvs_open("bmu", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "wifi_ssid", ssid);
            nvs_set_str(nvs, "wifi_pass", pass);
            nvs_commit(nvs);
            nvs_close(nvs);
        }

        /* Update status response */
        s_last_status.last_cmd = 3; /* wifi_config */
        s_last_status.battery_idx = 0xFF;
        s_last_status.result = 0; /* OK */

        /* Notify status change */
        if (s_status_val_handle != 0) {
            ble_gatts_chr_updated(s_status_val_handle);
        }

        /* TODO: trigger WiFi reconnect with new credentials */
        /* This requires firmware support to re-read NVS and reconnect */
        return 0;
    }
```

- [ ] **Step 4: Build to verify**

Run:
```bash
cd firmware-idf && idf.py build 2>&1 | tail -5
```
Expected: `Project build complete`

- [ ] **Step 5: Commit**

```bash
git add firmware-idf/components/bmu_ble/bmu_ble_control_svc.cpp
git commit -m "feat(ble): add WiFi Config characteristic (0x0034)"
```

---

### Task 5: Add BLE WiFi Status characteristic (0x0035) + notify timer

**Files:**
- Modify: `firmware-idf/components/bmu_ble/bmu_ble_control_svc.cpp`
- Modify: `firmware-idf/components/bmu_ble/include/bmu_ble_internal.h`

- [ ] **Step 1: Add static variables for WiFi status**

Near the top of bmu_ble_control_svc.cpp, add:

```c
static uint16_t s_wifi_sts_val_handle = 0;
static esp_timer_handle_t s_wifi_notify_timer = NULL;

/* Packed WiFi status: ssid(32) + ip(16) + rssi(1) + connected(1) = 50 bytes */
typedef struct __attribute__((packed)) {
    char    ssid[32];
    char    ip[16];
    int8_t  rssi;
    uint8_t connected;
} wifi_status_payload_t;
```

- [ ] **Step 2: Add WiFi Status read handler**

In the access callback switch, add after the WIFI_CONFIG case:

```c
    case CTRL_CHR_WIFI_STATUS: {
        if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_REQ_NOT_SUPPORTED;

        wifi_status_payload_t payload = {};
        payload.connected = bmu_wifi_is_connected() ? 1 : 0;
        if (payload.connected) {
            bmu_wifi_get_ip(payload.ip, sizeof(payload.ip));
            bmu_wifi_get_rssi(&payload.rssi);
            /* Read SSID from NVS */
            nvs_handle_t nvs;
            if (nvs_open("bmu", NVS_READONLY, &nvs) == ESP_OK) {
                size_t len = sizeof(payload.ssid);
                nvs_get_str(nvs, "wifi_ssid", payload.ssid, &len);
                nvs_close(nvs);
            }
        }

        os_mbuf_append(ctxt->om, &payload, sizeof(payload));
        return 0;
    }
```

- [ ] **Step 3: Add characteristic entries to GATT table**

In the `s_ctrl_chr_defs[]` array, before the terminator `{0}`, add:

```c
    /* WiFi Config — write encrypted */
    {
        .uuid = &s_wifi_cfg_chr_uuid.u,
        .access_cb = ctrl_access_cb,
        .arg = (void *)(uintptr_t)CTRL_CHR_WIFI_CONFIG,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
    },
    /* WiFi Status — read + notify */
    {
        .uuid = &s_wifi_sts_chr_uuid.u,
        .access_cb = ctrl_access_cb,
        .arg = (void *)(uintptr_t)CTRL_CHR_WIFI_STATUS,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_wifi_sts_val_handle,
    },
```

- [ ] **Step 4: Add notify timer functions**

Add the timer callback and start/stop functions:

```c
static void wifi_notify_cb(void *arg)
{
    (void)arg;
    if (s_wifi_sts_val_handle != 0) {
        ble_gatts_chr_updated(s_wifi_sts_val_handle);
    }
}

void bmu_ble_wifi_notify_start(void)
{
    if (s_wifi_notify_timer != NULL) return;
    const esp_timer_create_args_t args = {
        .callback = wifi_notify_cb,
        .arg = NULL,
        .name = "ble_wifi_ntf",
    };
    esp_timer_create(&args, &s_wifi_notify_timer);
    esp_timer_start_periodic(s_wifi_notify_timer, 10000000ULL); /* 10s */
}

void bmu_ble_wifi_notify_stop(void)
{
    if (s_wifi_notify_timer != NULL) {
        esp_timer_stop(s_wifi_notify_timer);
        esp_timer_delete(s_wifi_notify_timer);
        s_wifi_notify_timer = NULL;
    }
}
```

- [ ] **Step 5: Add declarations to bmu_ble_internal.h**

After the `bmu_ble_system_notify_stop` declaration, add:

```c
void bmu_ble_wifi_notify_start(void);
void bmu_ble_wifi_notify_stop(void);
```

- [ ] **Step 6: Start WiFi notify in bmu_ble.cpp on connect**

In `bmu_ble.cpp`, in the `BLE_GAP_EVENT_CONNECT` handler (around line 109), add after `bmu_ble_system_notify_start()`:

```c
                bmu_ble_wifi_notify_start();
```

And in `BLE_GAP_EVENT_DISCONNECT` (around line 133), add after `bmu_ble_system_notify_stop()`:

```c
            bmu_ble_wifi_notify_stop();
```

- [ ] **Step 7: Build to verify**

Run:
```bash
cd firmware-idf && idf.py build 2>&1 | tail -5
```
Expected: `Project build complete`

- [ ] **Step 8: Flash and verify BLE advertising**

Run:
```bash
cd firmware-idf && idf.py -p /dev/cu.usbmodem3101 flash
```

Capture logs to confirm BLE still starts:
```bash
# Look for "BLE active — advertising 'KXKM-BMU'" in serial output
```

- [ ] **Step 9: Commit**

```bash
git add firmware-idf/components/bmu_ble/bmu_ble_control_svc.cpp \
        firmware-idf/components/bmu_ble/bmu_ble.cpp \
        firmware-idf/components/bmu_ble/include/bmu_ble_internal.h
git commit -m "feat(ble): add WiFi Status characteristic (0x0035) + notify timer"
```

---

### Task 6: Integration build + flash test

**Files:** None (verification only)

- [ ] **Step 1: Clean build**

```bash
cd firmware-idf && idf.py build 2>&1 | tail -10
```
Expected: `Project build complete`, binary size < 2 MB (OTA partition limit)

- [ ] **Step 2: Flash to device**

```bash
cd firmware-idf && idf.py -p /dev/cu.usbmodem3101 flash
```

- [ ] **Step 3: Verify serial logs**

Capture logs and confirm:
- `BLE active — advertising 'KXKM-BMU'` (BLE OK)
- No crash/watchdog after boot
- WiFi init (OK or graceful failure)

- [ ] **Step 4: Test HTTP routes (if WiFi connected)**

```bash
curl http://<BMU_IP>/api/system
# Expected: {"fw_version":"0.4.0","uptime_s":...,"heap_free":...,"nb_ina":...}

curl http://<BMU_IP>/api/solar
# Expected: {"valid":false} (no VE.Direct connected) or solar data
```

- [ ] **Step 5: Final commit with version bump**

```bash
# Update version in sdkconfig.defaults
sed -i '' 's/CONFIG_APP_PROJECT_VER="0.4.0"/CONFIG_APP_PROJECT_VER="0.5.0"/' firmware-idf/sdkconfig.defaults
git add firmware-idf/sdkconfig.defaults
git commit -m "chore: bump firmware to v0.5.0 (app API ready)"
```

---

## Plan Roadmap (subsequent plans)

| Plan | Scope | Depends on | Estimated |
|------|-------|-----------|-----------|
| **Plan 1** (this) | Firmware: BLE WiFi chars + HTTP routes | — | 5 tasks |
| **Plan 2** | KMP shared module: model, db, transport, auth, sync | Plan 1 | ~15 tasks |
| **Plan 3** | Android app: Compose UI, 5 screens, navigation | Plan 2 | ~12 tasks |
| **Plan 4** | iOS app: SwiftUI, 5 screens, navigation | Plan 2 | ~12 tasks |
| **Plan 5** | kxkm-ai API: FastAPI service, InfluxDB queries | Plan 1 | ~5 tasks |

Plans 3 and 4 can run in parallel. Plan 5 can run in parallel with Plans 2-4.
