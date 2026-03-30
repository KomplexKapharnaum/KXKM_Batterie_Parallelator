# ESP-IDF Phase 3: Web Server + SD Storage + WiFi

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add WiFi connectivity, HTTP server with battery API + WebSocket, SD card logging, and NVS credential storage — replacing Arduino's AsyncWebServer, WebSocketsServer, SD, WiFi, and EEPROM libraries with ESP-IDF native equivalents.

**Architecture:** Three new components: `bmu_wifi` (esp_wifi + event loop), `bmu_web` (esp_http_server + WebSocket on same port 80), `bmu_storage` (SD via SDMMC/SPI + NVS for credentials). Web assets served from SPIFFS partition. Security (rate limiting, token auth) ported as pure C functions. Audit fix CRIT-D applied: mutation routes use POST, not GET.

**Tech Stack:** ESP-IDF v5.3, `esp_http_server`, `esp_wifi`, `cJSON`, SDMMC/SPI, NVS, SPIFFS

**Branch:** `feat/esp-idf-phase3-web-storage`

---

## File Structure

```
firmware-idf/components/
├── bmu_wifi/
│   ├── CMakeLists.txt
│   ├── include/bmu_wifi.h              # WiFi STA init + event handler
│   └── bmu_wifi.cpp
├── bmu_web/
│   ├── CMakeLists.txt
│   ├── include/bmu_web.h               # HTTP server + WebSocket API
│   ├── include/bmu_web_security.h      # Token auth + rate limit (pure C)
│   ├── bmu_web.cpp                     # Routes, handlers, WS
│   └── bmu_web_security.cpp            # Constant-time compare, rate limiter
├── bmu_storage/
│   ├── CMakeLists.txt
│   ├── include/bmu_storage.h           # SD logger + NVS API
│   └── bmu_storage.cpp
firmware-idf/
├── data/                               # SPIFFS web assets
│   ├── index.html
│   ├── style.css
│   └── script.js
firmware-idf/main/
└── main.cpp                            # Updated: WiFi + Web + Storage init
```

---

### Task 1: bmu_wifi — WiFi Station Mode

**Files:**
- Create: `firmware-idf/components/bmu_wifi/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_wifi/include/bmu_wifi.h`
- Create: `firmware-idf/components/bmu_wifi/bmu_wifi.cpp`

WiFi STA with event-driven connection management. Credentials from Kconfig (will move to NVS later).

API:
```cpp
esp_err_t bmu_wifi_init(void);           // Init NVS + netif + event loop + connect
bool bmu_wifi_is_connected(void);        // Check connection status
esp_err_t bmu_wifi_get_ip(char *buf, size_t len); // Get IP string
```

Implementation uses `esp_event_handler_register` for WIFI_EVENT and IP_EVENT. Auto-reconnect on disconnect. Kconfig entries for SSID/password.

Kconfig additions in a new `firmware-idf/components/bmu_wifi/Kconfig`:
```
menu "BMU WiFi"
    config BMU_WIFI_SSID
        string "WiFi SSID"
        default "KXKM-BMU"
    config BMU_WIFI_PASSWORD
        string "WiFi Password"
        default ""
    config BMU_WIFI_MAX_RETRY
        int "Max reconnect attempts"
        default 10
endmenu
```

- [ ] **Step 1: Create all 3 files + Kconfig**
- [ ] **Step 2: Commit**
```bash
git commit -m "feat(idf): add bmu_wifi — WiFi STA with auto-reconnect and Kconfig credentials"
```

---

### Task 2: bmu_web — HTTP Server + WebSocket + Security

**Files:**
- Create: `firmware-idf/components/bmu_web/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_web/include/bmu_web.h`
- Create: `firmware-idf/components/bmu_web/include/bmu_web_security.h`
- Create: `firmware-idf/components/bmu_web/bmu_web.cpp`
- Create: `firmware-idf/components/bmu_web/bmu_web_security.cpp`

This is the biggest task. The web server replaces AsyncWebServer + WebSocketsServer.

**Routes (audit fix CRIT-D: mutations use POST):**

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/` | No | Serve index.html from SPIFFS |
| GET | `/style.css` | No | Serve CSS |
| GET | `/script.js` | No | Serve JS |
| GET | `/api/batteries` | No | JSON array of battery status (cJSON) |
| POST | `/api/battery/switch_on` | Token + rate limit | Switch battery ON |
| POST | `/api/battery/switch_off` | Token + rate limit | Switch battery OFF |
| GET | `/api/log` | No | Last N lines of SD log (chunked) |
| WS | `/ws` | Token in first message | Real-time battery telemetry |

**bmu_web.h:**
```cpp
#pragma once
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "esp_err.h"

typedef struct {
    bmu_protection_ctx_t    *prot;
    bmu_battery_manager_t   *mgr;
} bmu_web_ctx_t;

esp_err_t bmu_web_start(bmu_web_ctx_t *ctx);
esp_err_t bmu_web_stop(void);
```

**bmu_web_security.h:**
```cpp
#pragma once
#include <stdint.h>
#include <stdbool.h>

bool bmu_web_token_valid(const char *provided, const char *configured);
bool bmu_web_rate_check(uint32_t client_ip, uint32_t now_ms);
```

Security: constant-time token compare (ported from WebRouteSecurity.cpp), rate limiter with 16 slots (upgraded from 8 per audit MED-4), LRU eviction.

**Key implementation details:**
- `esp_http_server` handles HTTP + WS on same port 80
- WebSocket: `httpd_ws_send_frame()` for push, authenticated via first message token
- cJSON for battery status JSON generation
- SPIFFS for static web assets (index.html, style.css, script.js)
- `/api/log` uses chunked response to avoid OOM (audit fix MED-9)
- POST body parsed for `battery` index + `token` (not query string — audit fix MED-5)

- [ ] **Step 1: Create all 5 files**
- [ ] **Step 2: Commit**
```bash
git commit -m "feat(idf): add bmu_web — esp_http_server + WebSocket + token auth + rate limiter"
```

---

### Task 3: bmu_storage — SD Logger + NVS

**Files:**
- Create: `firmware-idf/components/bmu_storage/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_storage/include/bmu_storage.h`
- Create: `firmware-idf/components/bmu_storage/bmu_storage.cpp`

**SD Logger** replaces SDLogger class. Uses ESP-IDF VFS + SDMMC (or SPI fallback for BOX-3 PMOD2).

**NVS** replaces EEPROM for WiFi credentials, MQTT config, admin token.

**bmu_storage.h:**
```cpp
#pragma once
#include "esp_err.h"

// SD card (SPI mode on PMOD2: MOSI=GPIO11, MISO=GPIO13, CLK=GPIO12, CS=GPIO10)
esp_err_t bmu_sd_init(void);
esp_err_t bmu_sd_log_line(const char *line);
esp_err_t bmu_sd_read_last_lines(char *buf, size_t buf_size, int max_lines);
bool bmu_sd_is_mounted(void);

// NVS
esp_err_t bmu_nvs_init(void);
esp_err_t bmu_nvs_get_str(const char *key, char *buf, size_t len);
esp_err_t bmu_nvs_set_str(const char *key, const char *value);
```

SD pin mapping from BOX-3 PMOD2:
- MOSI = GPIO11 (PMOD2 IO7)
- MISO = GPIO13 (PMOD2 IO1)
- CLK = GPIO12 (PMOD2 IO3)
- CS = GPIO10 (PMOD2 IO5)

CSV format preserved: `;` separator, same fields as Arduino version.

- [ ] **Step 1: Create all 3 files**
- [ ] **Step 2: Commit**
```bash
git commit -m "feat(idf): add bmu_storage — SD logger (SPI on PMOD2) + NVS credentials"
```

---

### Task 4: Web Assets in SPIFFS

**Files:**
- Create: `firmware-idf/data/index.html`
- Create: `firmware-idf/data/style.css`
- Create: `firmware-idf/data/script.js`

Port the existing web UI from embedded string headers to SPIFFS files. Fix the JS client to use POST for mutations and include token in request body (audit CRIT-D fix).

**script.js key change:**
```javascript
function switchBattery(index, state) {
    fetch(`/api/battery/switch_${state ? 'on' : 'off'}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ battery: index, token: getToken() })
    });
}
```

- [ ] **Step 1: Create web assets from existing embedded headers**
- [ ] **Step 2: Commit**
```bash
git commit -m "feat(idf): add SPIFFS web assets — POST mutations + token auth (CRIT-D fix)"
```

---

### Task 5: Update main.cpp — Full Phase 3 Integration

**Files:**
- Modify: `firmware-idf/main/CMakeLists.txt`
- Modify: `firmware-idf/main/main.cpp`

Add WiFi, Web, and Storage init to app_main. Init order:
1. NVS init (required by WiFi)
2. WiFi init + connect
3. SD init
4. I2C + INA + TCA + protection (unchanged)
5. Web server start (needs protection + battery manager handles)
6. Protection loop

```cpp
// New init sequence in app_main:
bmu_nvs_init();
bmu_wifi_init();
bmu_sd_init();
// ... existing I2C + protection init ...
bmu_web_ctx_t web_ctx = { .prot = &prot, .mgr = &mgr };
bmu_web_start(&web_ctx);
```

- [ ] **Step 1: Update CMakeLists + main.cpp**
- [ ] **Step 2: Commit**
```bash
git commit -m "feat(idf): Phase 3 complete — WiFi + Web + SD integrated in main"
```
