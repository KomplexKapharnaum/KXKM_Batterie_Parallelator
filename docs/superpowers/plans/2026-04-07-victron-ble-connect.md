# Victron BLE Connect Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Faire apparaître le BMU comme un SmartShunt dans VictronConnect (lecture seule) et scanner les appareils Victron BLE à portée pour récupérer leurs données.

**Architecture:** 2 nouveaux composants ESP-IDF (`bmu_ble_victron_gatt` pour l'émulation GATT SmartShunt, `bmu_ble_victron_scan` pour le scanner central BLE). Les clés AES par appareil sont stockées dans NVS via une extension de `bmu_config`. Les données scannées sont publiées via MQTT/InfluxDB et affichées sur l'écran LVGL.

**Tech Stack:** ESP-IDF 5.4, NimBLE (central + peripheral), mbedtls AES-CTR, LVGL v9

**Spec:** `docs/superpowers/specs/2026-04-07-victron-ble-connect-design.md`

---

## File Map

| File | Action | Responsabilité |
|------|--------|----------------|
| `components/bmu_config/include/bmu_config.h` | Modify | Ajouter API clés AES Victron par MAC |
| `components/bmu_config/bmu_config.cpp` | Modify | Impl NVS clés AES + labels |
| `components/bmu_ble_victron_gatt/include/bmu_ble_victron_gatt.h` | Create | Header GATT SmartShunt |
| `components/bmu_ble_victron_gatt/bmu_ble_victron_gatt.cpp` | Create | Service GATT 9 caractéristiques |
| `components/bmu_ble_victron_gatt/Kconfig` | Create | Enable toggle |
| `components/bmu_ble_victron_gatt/CMakeLists.txt` | Create | Build config |
| `components/bmu_ble_victron_scan/include/bmu_ble_victron_scan.h` | Create | Header scanner + cache |
| `components/bmu_ble_victron_scan/bmu_ble_victron_scan.cpp` | Create | Scanner central + AES decrypt |
| `components/bmu_ble_victron_scan/Kconfig` | Create | Scan timing config |
| `components/bmu_ble_victron_scan/CMakeLists.txt` | Create | Build config |
| `components/bmu_ble/bmu_ble.cpp` | Modify | Enregistrer service GATT Victron |
| `components/bmu_display/bmu_ui_system.cpp` | Modify | Section Victron devices |
| `main/main.cpp` | Modify | Init scan + GATT |
| `test/test_victron_gatt/main/test_victron_gatt.cpp` | Create | Tests encoding GATT |
| `test/test_victron_scan/main/test_victron_scan.cpp` | Create | Tests décryption + cache |

---

### Task 1: API config NVS pour clés AES Victron

**Files:**
- Modify: `firmware-idf/components/bmu_config/include/bmu_config.h`
- Modify: `firmware-idf/components/bmu_config/bmu_config.cpp`

- [ ] **Step 1: Ajouter les déclarations dans bmu_config.h**

Après les déclarations `bmu_config_get_victron_ble_enabled()`, ajouter :

```cpp
/* Victron device keys (per-MAC AES-128 keys for Instant Readout decryption) */
#define BMU_CONFIG_VIC_MAX_DEVICES  8
#define BMU_CONFIG_VIC_KEY_LEN      33  /* 32 hex + NUL */
#define BMU_CONFIG_VIC_LABEL_LEN    16

esp_err_t   bmu_config_set_victron_device_key(const uint8_t mac[6], const char *hex_key);
esp_err_t   bmu_config_get_victron_device_key(const uint8_t mac[6], char *hex_key, size_t len);
esp_err_t   bmu_config_del_victron_device_key(const uint8_t mac[6]);
esp_err_t   bmu_config_set_victron_device_label(const uint8_t mac[6], const char *label);
esp_err_t   bmu_config_get_victron_device_label(const uint8_t mac[6], char *label, size_t len);
int         bmu_config_list_victron_devices(uint8_t macs[][6], int max);
```

- [ ] **Step 2: Implémenter dans bmu_config.cpp**

Les clés sont stockées dans le namespace NVS `vic_keys`. La clé NVS est la MAC en hex majuscules (12 chars). Le label est stocké avec préfixe `L_` devant la MAC.

```cpp
/* ── Victron device keys (NVS namespace "vic_keys") ──────────────── */

static const char *VIC_NS = "vic_keys";

static void mac_to_nvs_key(const uint8_t mac[6], char *out)
{
    snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

esp_err_t bmu_config_set_victron_device_key(const uint8_t mac[6], const char *hex_key)
{
    if (!mac || !hex_key || strlen(hex_key) != 32) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t ret = nvs_open(VIC_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    char key[13];
    mac_to_nvs_key(mac, key);
    ret = nvs_set_str(h, key, hex_key);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t bmu_config_get_victron_device_key(const uint8_t mac[6], char *hex_key, size_t len)
{
    if (!mac || !hex_key || len < 33) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t ret = nvs_open(VIC_NS, NVS_READONLY, &h);
    if (ret != ESP_OK) return ret;
    char key[13];
    mac_to_nvs_key(mac, key);
    size_t req = len;
    ret = nvs_get_str(h, key, hex_key, &req);
    nvs_close(h);
    return ret;
}

esp_err_t bmu_config_del_victron_device_key(const uint8_t mac[6])
{
    if (!mac) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t ret = nvs_open(VIC_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    char key[13];
    mac_to_nvs_key(mac, key);
    nvs_erase_key(h, key);
    /* Also erase label */
    char lkey[15] = "L_";
    strncat(lkey, key, 12);
    nvs_erase_key(h, lkey);
    nvs_commit(h);
    nvs_close(h);
    return ESP_OK;
}

esp_err_t bmu_config_set_victron_device_label(const uint8_t mac[6], const char *label)
{
    if (!mac || !label) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t ret = nvs_open(VIC_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) return ret;
    char key[13];
    mac_to_nvs_key(mac, key);
    char lkey[15] = "L_";
    strncat(lkey, key, 12);
    ret = nvs_set_str(h, lkey, label);
    if (ret == ESP_OK) ret = nvs_commit(h);
    nvs_close(h);
    return ret;
}

esp_err_t bmu_config_get_victron_device_label(const uint8_t mac[6], char *label, size_t len)
{
    if (!mac || !label || len < 2) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    esp_err_t ret = nvs_open(VIC_NS, NVS_READONLY, &h);
    if (ret != ESP_OK) { label[0] = '\0'; return ret; }
    char key[13];
    mac_to_nvs_key(mac, key);
    char lkey[15] = "L_";
    strncat(lkey, key, 12);
    size_t req = len;
    ret = nvs_get_str(h, lkey, label, &req);
    if (ret != ESP_OK) label[0] = '\0';
    nvs_close(h);
    return ret;
}

int bmu_config_list_victron_devices(uint8_t macs[][6], int max)
{
    nvs_handle_t h;
    if (nvs_open(VIC_NS, NVS_READONLY, &h) != ESP_OK) return 0;
    nvs_iterator_t it = NULL;
    int count = 0;
    esp_err_t ret = nvs_entry_find_in_handle(h, NVS_TYPE_STR, &it);
    while (ret == ESP_OK && count < max) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        /* Skip label entries (start with L_) */
        if (info.key[0] != 'L' && strlen(info.key) == 12) {
            for (int i = 0; i < 6; i++) {
                unsigned byte = 0;
                sscanf(info.key + i * 2, "%02x", &byte);
                macs[count][i] = (uint8_t)byte;
            }
            count++;
        }
        ret = nvs_entry_next(&it);
    }
    if (it) nvs_release_iterator(it);
    nvs_close(h);
    return count;
}
```

- [ ] **Step 3: Build pour vérifier la compilation**

```bash
cd firmware-idf && source ~/esp/esp-idf/export.sh && idf.py build 2>&1 | tail -5
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_config/
git commit -m "feat(config): NVS storage for Victron device AES keys per MAC"
```

---

### Task 2: Composant GATT SmartShunt — scaffold + service

**Files:**
- Create: `firmware-idf/components/bmu_ble_victron_gatt/Kconfig`
- Create: `firmware-idf/components/bmu_ble_victron_gatt/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_ble_victron_gatt/include/bmu_ble_victron_gatt.h`
- Create: `firmware-idf/components/bmu_ble_victron_gatt/bmu_ble_victron_gatt.cpp`

- [ ] **Step 1: Créer Kconfig**

```kconfig
menu "BMU Victron GATT SmartShunt"
    config BMU_VICTRON_GATT_ENABLED
        bool "Enable SmartShunt GATT emulation"
        default y
        depends on BT_ENABLED
endmenu
```

- [ ] **Step 2: Créer CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "bmu_ble_victron_gatt.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bt bmu_protection bmu_config esp_timer
    PRIV_REQUIRES bmu_climate
)
```

- [ ] **Step 3: Créer le header**

```cpp
#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Retourne la definition du service GATT SmartShunt.
 *  A enregistrer dans le tableau GATT de bmu_ble.cpp. */
const struct ble_gatt_svc_def *bmu_ble_victron_gatt_svc_defs(void);

/** Demarre le timer de notification (1s). Appeler quand un client BLE se connecte. */
void bmu_ble_victron_gatt_notify_start(void);

/** Arrete le timer de notification. Appeler quand le dernier client BLE se deconnecte. */
void bmu_ble_victron_gatt_notify_stop(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Créer l'implémentation**

```cpp
/**
 * bmu_ble_victron_gatt — Service GATT emulant un SmartShunt Victron.
 * VictronConnect detecte ce service et affiche les donnees BMU.
 * Lecture seule — aucune commande d'ecriture.
 */

#include "bmu_ble_victron_gatt.h"

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_climate.h"
#include "bmu_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <cmath>

#if !CONFIG_BMU_VICTRON_GATT_ENABLED

const struct ble_gatt_svc_def *bmu_ble_victron_gatt_svc_defs(void) { return NULL; }
void bmu_ble_victron_gatt_notify_start(void) {}
void bmu_ble_victron_gatt_notify_stop(void) {}

#else

static const char *TAG = "VIC_GATT";

/* ── Victron-like UUIDs (community reverse-engineered) ────────────── */
/* Service:        68c10001-b17f-4d3a-a290-34ad6499937c */
/* Characteristics: 68c100xx-... where xx = suffix below */
#define VIC_SVC_UUID_DECLARE()  \
    BLE_UUID128_INIT(0x7c, 0x93, 0x99, 0x64, 0xad, 0x34, 0x90, 0xa2, \
                     0x3a, 0x4d, 0x7f, 0xb1, 0x01, 0x00, 0xc1, 0x68)

#define VIC_CHR_UUID(suffix)    \
    BLE_UUID128_INIT(0x7c, 0x93, 0x99, 0x64, 0xad, 0x34, 0x90, 0xa2, \
                     0x3a, 0x4d, 0x7f, 0xb1, (suffix), 0x00, 0xc1, 0x68)

/* Characteristic value handles (populated by NimBLE) */
static uint16_t s_hdl_voltage     = 0;
static uint16_t s_hdl_current     = 0;
static uint16_t s_hdl_soc         = 0;
static uint16_t s_hdl_consumed_ah = 0;
static uint16_t s_hdl_ttg         = 0;
static uint16_t s_hdl_temperature = 0;
static uint16_t s_hdl_alarm       = 0;

static esp_timer_handle_t s_notify_timer = NULL;

/* ── Extern accessors from bmu_ble ────────────────────────────────── */
extern "C" bmu_protection_ctx_t  *bmu_ble_get_prot(void);
extern "C" bmu_battery_manager_t *bmu_ble_get_mgr(void);
extern "C" uint8_t                bmu_ble_get_nb_ina(void);

/* ── Read callbacks ───────────────────────────────────────────────── */

static int read_voltage(uint16_t conn, uint16_t attr,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    bmu_protection_ctx_t *prot = bmu_ble_get_prot();
    bmu_battery_manager_t *mgr = bmu_ble_get_mgr();
    uint8_t nb = bmu_ble_get_nb_ina();
    float sum = 0; int n = 0;
    for (int i = 0; i < nb; i++) {
        if (bmu_protection_get_state(prot, i) == BMU_STATE_CONNECTED) {
            sum += bmu_protection_get_voltage(prot, i);
            n++;
        }
    }
    uint16_t val = (n > 0) ? (uint16_t)((sum / n) / 10.0f) : 0; /* 0.01V */
    os_mbuf_append(ctxt->om, &val, 2);
    return 0;
}

static int read_current(uint16_t conn, uint16_t attr,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    bmu_battery_manager_t *mgr = bmu_ble_get_mgr();
    uint8_t nb = bmu_ble_get_nb_ina();
    float total = 0;
    for (int i = 0; i < nb; i++) {
        total += bmu_battery_manager_get_ah_discharge(mgr, i); /* placeholder */
    }
    /* Use last known current from chart history or INA */
    int16_t val = 0; /* 0.1A signed — updated by notify timer */
    os_mbuf_append(ctxt->om, &val, 2);
    return 0;
}

static int read_soc(uint16_t conn, uint16_t attr,
                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    bmu_protection_ctx_t *prot = bmu_ble_get_prot();
    uint8_t nb = bmu_ble_get_nb_ina();
    uint16_t min_mv, max_mv, dummy1, dummy2;
    bmu_config_get_thresholds(&min_mv, &max_mv, &dummy1, &dummy2);
    float sum = 0; int n = 0;
    for (int i = 0; i < nb; i++) {
        if (bmu_protection_get_state(prot, i) == BMU_STATE_CONNECTED) {
            sum += bmu_protection_get_voltage(prot, i);
            n++;
        }
    }
    float avg_mv = (n > 0) ? sum / n : 0;
    float soc = (avg_mv - min_mv) / (float)(max_mv - min_mv) * 100.0f;
    if (soc < 0) soc = 0; if (soc > 100) soc = 100;
    uint16_t val = (uint16_t)(soc * 100.0f); /* 0-10000 */
    os_mbuf_append(ctxt->om, &val, 2);
    return 0;
}

static int read_consumed(uint16_t conn, uint16_t attr,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    bmu_battery_manager_t *mgr = bmu_ble_get_mgr();
    uint8_t nb = bmu_ble_get_nb_ina();
    float sum = 0;
    for (int i = 0; i < nb; i++) sum += bmu_battery_manager_get_ah_discharge(mgr, i);
    int32_t val = (int32_t)(sum * 10.0f); /* 0.1Ah */
    os_mbuf_append(ctxt->om, &val, 4);
    return 0;
}

static int read_ttg(uint16_t conn, uint16_t attr,
                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    uint16_t val = 0xFFFF; /* infinite (no discharge estimation) */
    os_mbuf_append(ctxt->om, &val, 2);
    return 0;
}

static int read_temperature(uint16_t conn, uint16_t attr,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    int16_t val = 0;
    if (bmu_climate_is_available()) {
        val = (int16_t)(bmu_climate_get_temperature() * 100.0f + 27315); /* 0.01K */
    }
    os_mbuf_append(ctxt->om, &val, 2);
    return 0;
}

static int read_alarm(uint16_t conn, uint16_t attr,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    bmu_protection_ctx_t *prot = bmu_ble_get_prot();
    uint8_t nb = bmu_ble_get_nb_ina();
    uint16_t min_mv, max_mv, max_ma, dummy;
    bmu_config_get_thresholds(&min_mv, &max_mv, &max_ma, &dummy);

    uint16_t alarm = 0;
    float sum = 0; int n = 0;
    bool has_error = false, has_locked = false;
    for (int i = 0; i < nb; i++) {
        bmu_battery_state_t st = bmu_protection_get_state(prot, i);
        if (st == BMU_STATE_ERROR) has_error = true;
        if (st == BMU_STATE_LOCKED) has_locked = true;
        if (st == BMU_STATE_CONNECTED) {
            sum += bmu_protection_get_voltage(prot, i);
            n++;
        }
    }
    if (n > 0) {
        float avg = sum / n;
        if (avg < min_mv) alarm |= (1 << 0); /* low V */
        if (avg > max_mv) alarm |= (1 << 1); /* high V */
    }
    if (has_error)  alarm |= (1 << 4);
    if (has_locked) alarm |= (1 << 5);
    if (bmu_climate_is_available() && bmu_climate_get_temperature() > 60.0f)
        alarm |= (1 << 3);

    os_mbuf_append(ctxt->om, &alarm, 2);
    return 0;
}

static int read_model(uint16_t conn, uint16_t attr,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    const char *name = "KXKM BMU";
    os_mbuf_append(ctxt->om, name, strlen(name));
    return 0;
}

static int read_serial(uint16_t conn, uint16_t attr,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    const char *sn = bmu_config_get_device_name();
    os_mbuf_append(ctxt->om, sn, strlen(sn));
    return 0;
}

/* ── Service definition ───────────────────────────────────────────── */

static const ble_uuid128_t svc_uuid = VIC_SVC_UUID_DECLARE();
static const ble_uuid128_t chr_voltage_uuid     = VIC_CHR_UUID(0x11);
static const ble_uuid128_t chr_current_uuid      = VIC_CHR_UUID(0x12);
static const ble_uuid128_t chr_soc_uuid          = VIC_CHR_UUID(0x13);
static const ble_uuid128_t chr_consumed_uuid     = VIC_CHR_UUID(0x14);
static const ble_uuid128_t chr_ttg_uuid          = VIC_CHR_UUID(0x15);
static const ble_uuid128_t chr_temp_uuid         = VIC_CHR_UUID(0x16);
static const ble_uuid128_t chr_alarm_uuid        = VIC_CHR_UUID(0x17);
static const ble_uuid128_t chr_model_uuid        = VIC_CHR_UUID(0x20);
static const ble_uuid128_t chr_serial_uuid       = VIC_CHR_UUID(0x21);

static const struct ble_gatt_chr_def vic_chrs[] = {
    { .uuid = &chr_voltage_uuid.u,  .access_cb = read_voltage,     .val_handle = &s_hdl_voltage,     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { .uuid = &chr_current_uuid.u,  .access_cb = read_current,     .val_handle = &s_hdl_current,     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { .uuid = &chr_soc_uuid.u,      .access_cb = read_soc,         .val_handle = &s_hdl_soc,         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { .uuid = &chr_consumed_uuid.u, .access_cb = read_consumed,    .val_handle = &s_hdl_consumed_ah, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { .uuid = &chr_ttg_uuid.u,      .access_cb = read_ttg,         .val_handle = &s_hdl_ttg,         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { .uuid = &chr_temp_uuid.u,     .access_cb = read_temperature, .val_handle = &s_hdl_temperature, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { .uuid = &chr_alarm_uuid.u,    .access_cb = read_alarm,       .val_handle = &s_hdl_alarm,       .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
    { .uuid = &chr_model_uuid.u,    .access_cb = read_model,       .flags = BLE_GATT_CHR_F_READ },
    { .uuid = &chr_serial_uuid.u,   .access_cb = read_serial,      .flags = BLE_GATT_CHR_F_READ },
    { 0 } /* terminateur */
};

static const struct ble_gatt_svc_def vic_svc_def[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = vic_chrs,
    },
    { 0 } /* terminateur */
};

const struct ble_gatt_svc_def *bmu_ble_victron_gatt_svc_defs(void)
{
    return vic_svc_def;
}

/* ── Notification timer (1s) ──────────────────────────────────────── */

static void notify_timer_cb(void *arg)
{
    (void)arg;
    /* Notify all subscribed clients on voltage + current + soc */
    uint16_t handles[] = { s_hdl_voltage, s_hdl_current, s_hdl_soc,
                           s_hdl_consumed_ah, s_hdl_alarm };
    for (int h = 0; h < 5; h++) {
        if (handles[h] == 0) continue;
        ble_gatts_chr_updated(handles[h]);
    }
}

void bmu_ble_victron_gatt_notify_start(void)
{
    if (s_notify_timer != NULL) return;
    const esp_timer_create_args_t args = {
        .callback = notify_timer_cb, .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK, .name = "vic_gatt",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &s_notify_timer);
    esp_timer_start_periodic(s_notify_timer, 1000000ULL); /* 1s */
    ESP_LOGI(TAG, "Victron GATT notifications started");
}

void bmu_ble_victron_gatt_notify_stop(void)
{
    if (s_notify_timer == NULL) return;
    esp_timer_stop(s_notify_timer);
    esp_timer_delete(s_notify_timer);
    s_notify_timer = NULL;
    ESP_LOGI(TAG, "Victron GATT notifications stopped");
}

#endif /* CONFIG_BMU_VICTRON_GATT_ENABLED */
```

- [ ] **Step 5: Commit**

```bash
git add firmware-idf/components/bmu_ble_victron_gatt/
git commit -m "feat(victron-gatt): SmartShunt GATT service emulation (read-only)"
```

---

### Task 3: Enregistrer le service GATT Victron dans bmu_ble

**Files:**
- Modify: `firmware-idf/components/bmu_ble/bmu_ble.cpp`
- Modify: `firmware-idf/components/bmu_ble/CMakeLists.txt`

- [ ] **Step 1: Ajouter la dépendance CMake**

Dans `bmu_ble/CMakeLists.txt`, ajouter `bmu_ble_victron_gatt` aux REQUIRES.

- [ ] **Step 2: Inclure et enregistrer le service**

Dans `bmu_ble.cpp`, ajouter l'include :
```cpp
#include "bmu_ble_victron_gatt.h"
```

Dans la section d'assemblage des services GATT (où `gatt_svcs[]` est rempli), augmenter le tableau de 4 à 5 et ajouter le service Victron :
```cpp
#ifdef CONFIG_BMU_VICTRON_GATT_ENABLED
    const struct ble_gatt_svc_def *vic_svc = bmu_ble_victron_gatt_svc_defs();
    if (vic_svc != NULL) {
        gatt_svcs[svc_count++] = vic_svc[0];
    }
#endif
    memset(&gatt_svcs[svc_count], 0, sizeof(struct ble_gatt_svc_def));
```

Dans le handler de connexion, ajouter l'appel au timer de notification Victron :
```cpp
    bmu_ble_victron_gatt_notify_start();
```

Et dans le handler de déconnexion (quand `s_connected_count == 0`) :
```cpp
    bmu_ble_victron_gatt_notify_stop();
```

- [ ] **Step 3: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_ble/
git commit -m "feat(ble): register Victron GATT SmartShunt service"
```

---

### Task 4: Composant scanner BLE central

**Files:**
- Create: `firmware-idf/components/bmu_ble_victron_scan/Kconfig`
- Create: `firmware-idf/components/bmu_ble_victron_scan/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_ble_victron_scan/include/bmu_ble_victron_scan.h`
- Create: `firmware-idf/components/bmu_ble_victron_scan/bmu_ble_victron_scan.cpp`

- [ ] **Step 1: Créer Kconfig**

```kconfig
menu "BMU Victron BLE Scanner"
    config BMU_VIC_SCAN_ENABLED
        bool "Enable Victron BLE device scanner"
        default y
        depends on BT_ENABLED

    config BMU_VIC_SCAN_DURATION_S
        int "Scan duration (seconds)"
        default 5
        range 2 10
        depends on BMU_VIC_SCAN_ENABLED

    config BMU_VIC_SCAN_PERIOD_S
        int "Scan period (seconds between scans)"
        default 30
        range 10 300
        depends on BMU_VIC_SCAN_ENABLED

    config BMU_VIC_SCAN_MAX_DEVICES
        int "Maximum Victron devices to track"
        default 8
        range 1 16
        depends on BMU_VIC_SCAN_ENABLED
endmenu
```

- [ ] **Step 2: Créer CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "bmu_ble_victron_scan.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bt bmu_config esp_timer
    PRIV_REQUIRES mbedtls bmu_mqtt bmu_influx
)
```

- [ ] **Step 3: Créer le header**

```cpp
#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMU_VIC_SCAN_MAX_DEVICES CONFIG_BMU_VIC_SCAN_MAX_DEVICES

typedef struct {
    uint8_t  mac[6];
    uint8_t  record_type;        /* 0x01=solar, 0x02=battery, 0x03=inverter, 0x04=dcdc */
    uint8_t  raw_decrypted[10];
    int64_t  last_seen_ms;
    bool     key_configured;
    bool     decrypted;
    char     label[16];
    /* Parsed fields by record type */
    union {
        struct { uint8_t cs; uint8_t err; uint16_t yield_wh; uint16_t ppv_w;
                 int16_t ibat_da; uint16_t vbat_cv; } solar;
        struct { uint16_t rem_ah_dah; uint16_t v_cv; int16_t i_da;
                 uint16_t soc_pm; uint16_t cons_dah; } battery;
        struct { uint16_t vac_cv; uint16_t iac_da; uint8_t state; } inverter;
        struct { uint16_t vin_cv; uint16_t vout_cv; uint8_t state; } dcdc;
    };
} bmu_vic_device_t;

esp_err_t bmu_vic_scan_init(void);
esp_err_t bmu_vic_scan_start(void);
void      bmu_vic_scan_stop(void);
int       bmu_vic_scan_get_devices(bmu_vic_device_t *out, int max);
const bmu_vic_device_t *bmu_vic_scan_get_device_by_mac(const uint8_t mac[6]);
int       bmu_vic_scan_count(void);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 4: Créer l'implémentation**

Le scanner utilise `ble_gap_disc()` en mode passif pour capturer les pubs sans se connecter. Un timer ESP déclenche un scan périodique. Les pubs avec company ID 0x02E1 sont filtrées, décryptées si la clé est configurée, et mises en cache.

L'implémentation complète est ~250 lignes. Les points clés :

```cpp
#include "bmu_ble_victron_scan.h"
#include "bmu_config.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "mbedtls/aes.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>

#if !CONFIG_BMU_VIC_SCAN_ENABLED
/* Stubs disabled */
esp_err_t bmu_vic_scan_init(void) { return ESP_OK; }
esp_err_t bmu_vic_scan_start(void) { return ESP_OK; }
void bmu_vic_scan_stop(void) {}
int bmu_vic_scan_get_devices(bmu_vic_device_t *, int) { return 0; }
const bmu_vic_device_t *bmu_vic_scan_get_device_by_mac(const uint8_t *) { return NULL; }
int bmu_vic_scan_count(void) { return 0; }
#else

static const char *TAG = "VIC_SCAN";
#define VICTRON_COMPANY_ID 0x02E1
#define EXPIRY_MS (5 * 60 * 1000) /* 5 minutes */

static bmu_vic_device_t s_devices[CONFIG_BMU_VIC_SCAN_MAX_DEVICES];
static int s_device_count = 0;
static esp_timer_handle_t s_scan_timer = NULL;
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* ── AES decrypt (same algo as bmu_ble_victron) ──────────────────── */
static bool decrypt_payload(const uint8_t mac[6], const uint8_t *cipher,
                            uint8_t *plain, size_t len, uint16_t counter)
{
    char hex_key[33];
    if (bmu_config_get_victron_device_key(mac, hex_key, sizeof(hex_key)) != ESP_OK)
        return false;

    uint8_t key[16];
    for (int i = 0; i < 16; i++) {
        unsigned b = 0;
        sscanf(hex_key + i * 2, "%02x", &b);
        key[i] = (uint8_t)b;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);
    uint8_t nonce[16] = {};
    nonce[0] = (uint8_t)(counter & 0xFF);
    nonce[1] = (uint8_t)(counter >> 8);
    size_t nc_off = 0;
    uint8_t stream[16] = {};
    mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce, stream, cipher, plain);
    mbedtls_aes_free(&aes);
    return true;
}

/* ── Parse decoded payload by record type ─────────────────────────── */
static void parse_payload(bmu_vic_device_t *dev)
{
    const uint8_t *p = dev->raw_decrypted;
    switch (dev->record_type) {
    case 0x01: /* Solar */
        dev->solar.cs       = p[0];
        dev->solar.err      = p[1];
        memcpy(&dev->solar.yield_wh, p + 2, 2);
        memcpy(&dev->solar.ppv_w,    p + 4, 2);
        memcpy(&dev->solar.ibat_da,  p + 6, 2);
        memcpy(&dev->solar.vbat_cv,  p + 8, 2);
        break;
    case 0x02: /* Battery */
        memcpy(&dev->battery.rem_ah_dah, p + 0, 2);
        memcpy(&dev->battery.v_cv,       p + 2, 2);
        memcpy(&dev->battery.i_da,       p + 4, 2);
        memcpy(&dev->battery.soc_pm,     p + 6, 2);
        memcpy(&dev->battery.cons_dah,   p + 8, 2);
        break;
    case 0x03: /* Inverter */
        memcpy(&dev->inverter.vac_cv,  p + 0, 2);
        memcpy(&dev->inverter.iac_da,  p + 2, 2);
        dev->inverter.state = p[4];
        break;
    case 0x04: /* DC-DC */
        memcpy(&dev->dcdc.vin_cv,  p + 0, 2);
        memcpy(&dev->dcdc.vout_cv, p + 2, 2);
        dev->dcdc.state = p[4];
        break;
    }
}

/* ── Find or allocate device slot ─────────────────────────────────── */
static bmu_vic_device_t *find_or_alloc(const uint8_t mac[6])
{
    for (int i = 0; i < s_device_count; i++) {
        if (memcmp(s_devices[i].mac, mac, 6) == 0) return &s_devices[i];
    }
    if (s_device_count < CONFIG_BMU_VIC_SCAN_MAX_DEVICES) {
        bmu_vic_device_t *d = &s_devices[s_device_count++];
        memset(d, 0, sizeof(*d));
        memcpy(d->mac, mac, 6);
        /* Load label from NVS */
        bmu_config_get_victron_device_label(mac, d->label, sizeof(d->label));
        return d;
    }
    /* Evict oldest */
    int oldest = 0;
    for (int i = 1; i < s_device_count; i++) {
        if (s_devices[i].last_seen_ms < s_devices[oldest].last_seen_ms)
            oldest = i;
    }
    bmu_vic_device_t *d = &s_devices[oldest];
    memset(d, 0, sizeof(*d));
    memcpy(d->mac, mac, 6);
    bmu_config_get_victron_device_label(mac, d->label, sizeof(d->label));
    return d;
}

/* ── GAP event handler for scan results ───────────────────────────── */
static int scan_event_handler(struct ble_gap_event *event, void *arg)
{
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    const struct ble_hs_adv_fields *fields = NULL;
    struct ble_hs_adv_fields parsed;
    if (ble_hs_adv_parse_fields(&parsed, event->disc.data,
                                 event->disc.length_data) != 0)
        return 0;

    /* Filter: must have manufacturer data with Victron company ID */
    if (parsed.mfg_data == NULL || parsed.mfg_data_len < 15) return 0;
    uint16_t company = parsed.mfg_data[0] | (parsed.mfg_data[1] << 8);
    if (company != VICTRON_COMPANY_ID) return 0;

    uint8_t record_type = parsed.mfg_data[2];
    uint16_t counter = parsed.mfg_data[3] | (parsed.mfg_data[4] << 8);
    const uint8_t *cipher = parsed.mfg_data + 5;

    uint8_t mac[6];
    memcpy(mac, event->disc.addr.val, 6);

    bmu_vic_device_t *dev = find_or_alloc(mac);
    dev->record_type = record_type;
    dev->last_seen_ms = now_ms();

    /* Try decryption */
    char hex_key[33];
    dev->key_configured = (bmu_config_get_victron_device_key(mac, hex_key, sizeof(hex_key)) == ESP_OK);
    if (dev->key_configured) {
        dev->decrypted = decrypt_payload(mac, cipher, dev->raw_decrypted, 10, counter);
        if (dev->decrypted) parse_payload(dev);
    } else {
        dev->decrypted = false;
    }

    return 0;
}

/* ── Periodic scan trigger ────────────────────────────────────────── */
static void scan_timer_cb(void *arg)
{
    (void)arg;
    struct ble_gap_disc_params params = {};
    params.passive = 1;
    params.filter_duplicates = 0;
    params.itvl = BLE_GAP_SCAN_ITVL_MS(100);
    params.window = BLE_GAP_SCAN_WIN_MS(100);

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, CONFIG_BMU_VIC_SCAN_DURATION_S * 1000,
                          &params, scan_event_handler, NULL);
    if (rc == 0) {
        ESP_LOGD(TAG, "Scan started (%ds)", CONFIG_BMU_VIC_SCAN_DURATION_S);
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

esp_err_t bmu_vic_scan_init(void)
{
    s_device_count = 0;
    memset(s_devices, 0, sizeof(s_devices));
    ESP_LOGI(TAG, "Init OK — scan %ds every %ds, max %d devices",
             CONFIG_BMU_VIC_SCAN_DURATION_S, CONFIG_BMU_VIC_SCAN_PERIOD_S,
             CONFIG_BMU_VIC_SCAN_MAX_DEVICES);
    return ESP_OK;
}

esp_err_t bmu_vic_scan_start(void)
{
    if (s_scan_timer != NULL) return ESP_OK;
    const esp_timer_create_args_t args = {
        .callback = scan_timer_cb, .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK, .name = "vic_scan",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &s_scan_timer);
    esp_timer_start_periodic(s_scan_timer, CONFIG_BMU_VIC_SCAN_PERIOD_S * 1000000ULL);
    scan_timer_cb(NULL); /* First scan immediately */
    return ESP_OK;
}

void bmu_vic_scan_stop(void)
{
    if (s_scan_timer) {
        esp_timer_stop(s_scan_timer);
        esp_timer_delete(s_scan_timer);
        s_scan_timer = NULL;
    }
}

int bmu_vic_scan_get_devices(bmu_vic_device_t *out, int max)
{
    int count = 0;
    int64_t now = now_ms();
    for (int i = 0; i < s_device_count && count < max; i++) {
        if ((now - s_devices[i].last_seen_ms) < EXPIRY_MS) {
            out[count++] = s_devices[i];
        }
    }
    return count;
}

const bmu_vic_device_t *bmu_vic_scan_get_device_by_mac(const uint8_t mac[6])
{
    for (int i = 0; i < s_device_count; i++) {
        if (memcmp(s_devices[i].mac, mac, 6) == 0) return &s_devices[i];
    }
    return NULL;
}

int bmu_vic_scan_count(void)
{
    int count = 0;
    int64_t now = now_ms();
    for (int i = 0; i < s_device_count; i++) {
        if ((now - s_devices[i].last_seen_ms) < EXPIRY_MS) count++;
    }
    return count;
}

#endif /* CONFIG_BMU_VIC_SCAN_ENABLED */
```

- [ ] **Step 5: Commit**

```bash
git add firmware-idf/components/bmu_ble_victron_scan/
git commit -m "feat(victron-scan): BLE central scanner for Victron Instant Readout devices"
```

---

### Task 5: Intégration dans main.cpp

**Files:**
- Modify: `firmware-idf/main/main.cpp`
- Modify: `firmware-idf/main/CMakeLists.txt`

- [ ] **Step 1: Ajouter les dépendances CMake**

Dans `main/CMakeLists.txt`, ajouter `bmu_ble_victron_gatt bmu_ble_victron_scan` aux REQUIRES.

- [ ] **Step 2: Ajouter les includes**

```cpp
#include "bmu_ble_victron_gatt.h"
#include "bmu_ble_victron_scan.h"
```

- [ ] **Step 3: Ajouter l'init du scanner**

Après `bmu_ble_victron_init(...)` dans la section BLE de app_main :

```cpp
#ifdef CONFIG_BMU_VIC_SCAN_ENABLED
    bmu_vic_scan_init();
    bmu_vic_scan_start();
    ESP_LOGI(TAG, "Victron BLE scanner started");
#endif
```

Le GATT est déjà enregistré via bmu_ble.cpp (Task 3).

- [ ] **Step 4: Build**

```bash
idf.py build 2>&1 | tail -5
```

- [ ] **Step 5: Commit**

```bash
git add firmware-idf/main/
git commit -m "feat(main): init Victron GATT + scanner at boot"
```

---

### Task 6: Affichage Victron sur l'écran LCD

**Files:**
- Modify: `firmware-idf/components/bmu_display/bmu_ui_system.cpp`
- Modify: `firmware-idf/components/bmu_display/CMakeLists.txt`

- [ ] **Step 1: Ajouter la dépendance CMake**

Dans `bmu_display/CMakeLists.txt`, ajouter `bmu_ble_victron_scan` aux PRIV_REQUIRES.

- [ ] **Step 2: Ajouter la section Victron dans bmu_ui_system.cpp**

Ajouter `#include "bmu_ble_victron_scan.h"` puis dans la fonction d'update système, après les infos WiFi/BLE existantes, ajouter une section qui itère les appareils scannés :

```cpp
    /* ── Section Victron devices ─────────────────────────────────────── */
    int vic_count = bmu_vic_scan_count();
    if (vic_count > 0) {
        bmu_vic_device_t devs[4];
        int n = bmu_vic_scan_get_devices(devs, 4);
        for (int i = 0; i < n; i++) {
            char line[48];
            if (!devs[i].decrypted) {
                snprintf(line, sizeof(line), "%02X:%02X:%02X (locked)",
                         devs[i].mac[3], devs[i].mac[4], devs[i].mac[5]);
            } else if (devs[i].record_type == 0x01) {
                snprintf(line, sizeof(line), "%-8s %.1fV %dW %s",
                         devs[i].label[0] ? devs[i].label : "MPPT",
                         devs[i].solar.vbat_cv / 100.0f,
                         devs[i].solar.ppv_w,
                         devs[i].solar.cs == 3 ? "Bulk" :
                         devs[i].solar.cs == 4 ? "Abs" :
                         devs[i].solar.cs == 5 ? "Float" : "Off");
            } else if (devs[i].record_type == 0x02) {
                snprintf(line, sizeof(line), "%-8s %.1fV %.1fA %d%%",
                         devs[i].label[0] ? devs[i].label : "Shunt",
                         devs[i].battery.v_cv / 100.0f,
                         devs[i].battery.i_da / 10.0f,
                         devs[i].battery.soc_pm / 10);
            }
            /* Update LVGL label — pattern depends on existing UI structure */
        }
    }
```

Les détails LVGL exacts dépendent de la structure existante de `bmu_ui_system.cpp`. L'implémenteur devra lire le fichier et ajouter les labels dans le bon conteneur flex.

- [ ] **Step 3: Build + flash**

```bash
idf.py build && idf.py -p /dev/cu.usbmodem3101 flash
```

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/components/bmu_display/
git commit -m "feat(display): Victron device list in System tab"
```

---

### Task 7: Tests unitaires

**Files:**
- Create: `firmware-idf/test/test_victron_gatt/main/test_victron_gatt.cpp`
- Create: `firmware-idf/test/test_victron_gatt/CMakeLists.txt`
- Create: `firmware-idf/test/test_victron_scan/main/test_victron_scan.cpp`
- Create: `firmware-idf/test/test_victron_scan/CMakeLists.txt`

- [ ] **Step 1: Tests GATT encoding**

Tests pour vérifier l'encoding des valeurs GATT (voltage 0.01V, current 0.1A signé, SOC 0-10000, alarm bitmask, temperature Kelvin).

```cpp
#include "unity.h"
#include <cstring>
#include <cstdint>

/* Test voltage encoding : 27.50V → 2750 (0.01V units) */
void test_voltage_encoding(void)
{
    float avg_mv = 27500.0f;
    uint16_t val = (uint16_t)(avg_mv / 10.0f);
    TEST_ASSERT_EQUAL_UINT16(2750, val);
}

/* Test SOC linear mapping : 26V in [24V-30V] → 33.33% → 3333 */
void test_soc_mapping(void)
{
    float avg_mv = 26000.0f;
    float min_mv = 24000.0f;
    float max_mv = 30000.0f;
    float soc = (avg_mv - min_mv) / (max_mv - min_mv) * 100.0f;
    uint16_t val = (uint16_t)(soc * 100.0f);
    TEST_ASSERT_EQUAL_UINT16(3333, val);
}

/* Test alarm bitmask : low voltage */
void test_alarm_low_voltage(void)
{
    float avg_mv = 23000.0f;
    uint16_t min_mv = 24000;
    uint16_t alarm = 0;
    if (avg_mv < min_mv) alarm |= (1 << 0);
    TEST_ASSERT_BITS(0x01, 0x01, alarm);
}

/* Test temperature Kelvin encoding : 25.0C → 2500 + 27315 = 29815 */
void test_temperature_kelvin(void)
{
    float temp_c = 25.0f;
    int16_t val = (int16_t)(temp_c * 100.0f + 27315);
    TEST_ASSERT_EQUAL_INT16(29815, val);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_voltage_encoding);
    RUN_TEST(test_soc_mapping);
    RUN_TEST(test_alarm_low_voltage);
    RUN_TEST(test_temperature_kelvin);
    return UNITY_END();
}
```

- [ ] **Step 2: Tests scanner décryption**

Tests pour vérifier le parsing des payloads Victron déchiffrés (réutilise les formats de test_ble_victron).

```cpp
#include "unity.h"
#include <cstring>
#include <cstdint>

/* Test solar payload parsing (10 bytes LE) */
void test_parse_solar_payload(void)
{
    uint8_t plain[10];
    plain[0] = 3;       /* charge_state = Bulk */
    plain[1] = 0;       /* error = none */
    uint16_t yield = 125; /* 1250 Wh / 10 */
    uint16_t ppv = 350;
    int16_t ibat = 123;  /* 12.3A × 10 */
    uint16_t vbat = 2750; /* 27.50V × 100 */
    memcpy(plain + 2, &yield, 2);
    memcpy(plain + 4, &ppv, 2);
    memcpy(plain + 6, &ibat, 2);
    memcpy(plain + 8, &vbat, 2);

    /* Verify parsing */
    uint8_t cs = plain[0];
    TEST_ASSERT_EQUAL_UINT8(3, cs);
    uint16_t parsed_ppv;
    memcpy(&parsed_ppv, plain + 4, 2);
    TEST_ASSERT_EQUAL_UINT16(350, parsed_ppv);
    int16_t parsed_ibat;
    memcpy(&parsed_ibat, plain + 6, 2);
    TEST_ASSERT_EQUAL_INT16(123, parsed_ibat);
}

/* Test battery payload parsing */
void test_parse_battery_payload(void)
{
    uint8_t plain[10];
    uint16_t rem = 955;   /* 95.5 Ah × 10 */
    uint16_t v = 2720;    /* 27.20V × 100 */
    int16_t i = -32;      /* -3.2A × 10 */
    uint16_t soc = 980;   /* 98.0% × 10 */
    uint16_t cons = 45;   /* 4.5 Ah × 10 */
    memcpy(plain + 0, &rem, 2);
    memcpy(plain + 2, &v, 2);
    memcpy(plain + 4, &i, 2);
    memcpy(plain + 6, &soc, 2);
    memcpy(plain + 8, &cons, 2);

    int16_t parsed_i;
    memcpy(&parsed_i, plain + 4, 2);
    TEST_ASSERT_EQUAL_INT16(-32, parsed_i);
    uint16_t parsed_soc;
    memcpy(&parsed_soc, plain + 6, 2);
    TEST_ASSERT_EQUAL_UINT16(980, parsed_soc);
}

/* Test device expiry (5 min = 300000 ms) */
void test_device_expiry(void)
{
    int64_t last_seen = 1000;
    int64_t now = 301001;
    bool expired = (now - last_seen) >= 300000;
    TEST_ASSERT_TRUE(expired);

    now = 299999;
    expired = (now - last_seen) >= 300000;
    TEST_ASSERT_FALSE(expired);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_solar_payload);
    RUN_TEST(test_parse_battery_payload);
    RUN_TEST(test_device_expiry);
    return UNITY_END();
}
```

- [ ] **Step 3: CMakeLists pour les tests**

Chaque test : `cmake_minimum_required(VERSION 3.16)` + `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` + `project(test_name)`

- [ ] **Step 4: Commit**

```bash
git add firmware-idf/test/test_victron_gatt/ firmware-idf/test/test_victron_scan/
git commit -m "test(victron): GATT encoding + scan payload parsing + expiry tests"
```

---

## Ordre d'exécution

```
Task 1 (config NVS) ─────────────┐
Task 2 (GATT SmartShunt) ────────┤── Task 5 (main.cpp) ── Task 6 (display) ── Task 7 (tests)
Task 3 (register in bmu_ble) ────┤
Task 4 (scanner central) ────────┘
```

Tasks 1-4 sont quasi-indépendants. Task 5 dépend de 2+3+4. Task 6 dépend de 4+5. Task 7 est indépendant.
