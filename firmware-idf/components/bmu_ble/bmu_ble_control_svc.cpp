/**
 * @file bmu_ble_control_svc.cpp
 * @brief Service GATT Control — switch, reset, config (WRITE + pairing requis).
 *
 * Toutes les ecritures necessitent un lien chiffre (BLE_GATT_CHR_F_WRITE_ENC).
 * UUIDs : Service 0x0003, Chars 0x0030..0x0033.
 */
#include "sdkconfig.h"

#if CONFIG_BMU_BLE_ENABLED

#include "bmu_ble_internal.h"
#include "bmu_protection.h"
#include "bmu_config.h"
#include "bmu_wifi.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "os/os_mbuf.h"

#include <cstring>

static const char *TAG = "BLE_CTRL";

/* ── Struct status/response (3 octets) ───────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t last_cmd;      /* 0=switch, 1=reset, 2=config */
    uint8_t battery_idx;   /* 0xFF si N/A (config) */
    uint8_t result;        /* 0=OK, 1=invalid_idx, 2=locked, 3=error */
} ble_control_status_t;

static ble_control_status_t s_last_status = { 0, 0xFF, 0 };
static uint16_t s_status_val_handle = 0;

/* ── Envoi de la notification status ─────────────────────────────── */
static void notify_status(void)
{
    if (s_status_val_handle == 0) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&s_last_status, sizeof(s_last_status));
    if (om) {
        ble_gatts_notify_custom(0xFFFF, s_status_val_handle, om);
    }
}

/* ── UUIDs ────────────────────────────────────────────────────────── */
static ble_uuid128_t s_ctrl_svc_uuid     = BMU_BLE_UUID128_DECLARE(0x03, 0x00);
static ble_uuid128_t s_switch_chr_uuid   = BMU_BLE_UUID128_DECLARE(0x30, 0x00);
static ble_uuid128_t s_reset_chr_uuid    = BMU_BLE_UUID128_DECLARE(0x31, 0x00);
static ble_uuid128_t s_config_chr_uuid   = BMU_BLE_UUID128_DECLARE(0x32, 0x00);
static ble_uuid128_t s_status_chr_uuid   = BMU_BLE_UUID128_DECLARE(0x33, 0x00);
static ble_uuid128_t s_wifi_cfg_chr_uuid = BMU_BLE_UUID128_DECLARE(0x34, 0x00);
static ble_uuid128_t s_wifi_sts_chr_uuid = BMU_BLE_UUID128_DECLARE(0x35, 0x00);
static const ble_uuid128_t chr_uuid_bat_label =
    { BLE_UUID_TYPE_128, BMU_BLE_UUID128_DECLARE(0x36, 0x00) };

static uint16_t s_wifi_sts_val_handle = 0;
static esp_timer_handle_t s_wifi_notify_timer = NULL;

enum ctrl_chr_id {
    CTRL_CHR_SWITCH = 0,
    CTRL_CHR_RESET,
    CTRL_CHR_CONFIG,
    CTRL_CHR_STATUS,
    CTRL_CHR_WIFI_CONFIG,
    CTRL_CHR_WIFI_STATUS,
    CTRL_CHR_BAT_LABEL,
};

/* ── Helper : lire le payload d'un write dans un buffer ──────────── */
static int read_write_payload(struct ble_gatt_access_ctxt *ctxt,
                               uint8_t *buf, uint16_t max_len, uint16_t *out_len)
{
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > max_len) return -1;
    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, max_len, out_len);
    return rc;
}

/* ── Callback acces GATT ─────────────────────────────────────────── */
static int control_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int chr_id = (int)(intptr_t)arg;
    bmu_protection_ctx_t *prot = bmu_ble_get_prot();
    uint8_t nb_ina = bmu_ble_get_nb_ina();

    /* Status characteristic : READ seul */
    if (chr_id == CTRL_CHR_STATUS) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            int rc = os_mbuf_append(ctxt->om, &s_last_status, sizeof(s_last_status));
            return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Les 3 characteristics de controle : WRITE seul */
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t buf[16];
    uint16_t len = 0;

    switch (chr_id) {
    case CTRL_CHR_SWITCH: {
        if (read_write_payload(ctxt, buf, sizeof(buf), &len) != 0 || len != 2) {
            ESP_LOGW(TAG, "Switch: payload invalide (len=%d)", len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint8_t battery_idx = buf[0];
        uint8_t on_off = buf[1];

        s_last_status.last_cmd = 0;
        s_last_status.battery_idx = battery_idx;

        if (battery_idx >= nb_ina) {
            ESP_LOGW(TAG, "Switch: index invalide %d (max=%d)", battery_idx, nb_ina);
            s_last_status.result = 1; /* invalid_idx */
            notify_status();
            return 0;
        }

        esp_err_t ret = bmu_protection_web_switch(prot, battery_idx, on_off != 0);
        if (ret == ESP_OK) {
            s_last_status.result = 0;
            ESP_LOGI(TAG, "Switch bat[%d] -> %s", battery_idx, on_off ? "ON" : "OFF");
        } else if (ret == ESP_ERR_INVALID_ARG) {
            s_last_status.result = 1;
        } else if (ret == ESP_ERR_NOT_ALLOWED) {
            s_last_status.result = 2; /* locked */
        } else {
            s_last_status.result = 3; /* error */
        }
        notify_status();
        return 0;
    }

    case CTRL_CHR_RESET: {
        if (read_write_payload(ctxt, buf, sizeof(buf), &len) != 0 || len != 1) {
            ESP_LOGW(TAG, "Reset: payload invalide (len=%d)", len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint8_t battery_idx = buf[0];

        s_last_status.last_cmd = 1;
        s_last_status.battery_idx = battery_idx;

        if (battery_idx >= nb_ina) {
            ESP_LOGW(TAG, "Reset: index invalide %d", battery_idx);
            s_last_status.result = 1;
            notify_status();
            return 0;
        }

        esp_err_t ret = bmu_protection_reset_switch_count(prot, battery_idx);
        s_last_status.result = (ret == ESP_OK) ? 0 : 3;
        ESP_LOGI(TAG, "Reset switch count bat[%d] -> %s", battery_idx,
                 ret == ESP_OK ? "OK" : esp_err_to_name(ret));
        notify_status();
        return 0;
    }

    case CTRL_CHR_CONFIG: {
        if (read_write_payload(ctxt, buf, sizeof(buf), &len) != 0 || len != 8) {
            ESP_LOGW(TAG, "Config: payload invalide (len=%d)", len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        /* Extraire 4 uint16_t little-endian */
        uint16_t min_mv  = (uint16_t)(buf[0] | (buf[1] << 8));
        uint16_t max_mv  = (uint16_t)(buf[2] | (buf[3] << 8));
        uint16_t max_ma  = (uint16_t)(buf[4] | (buf[5] << 8));
        uint16_t diff_mv = (uint16_t)(buf[6] | (buf[7] << 8));

        s_last_status.last_cmd = 2;
        s_last_status.battery_idx = 0xFF;

        /* Validation des plages */
        if (min_mv < 20000 || min_mv > 30000 ||
            max_mv < 25000 || max_mv > 35000 ||
            max_ma < 1000  || max_ma > 50000 ||
            diff_mv < 100  || diff_mv > 5000) {
            ESP_LOGW(TAG, "Config: valeurs hors plage min=%u max=%u maxI=%u diff=%u",
                     min_mv, max_mv, max_ma, diff_mv);
            s_last_status.result = 3; /* error */
            notify_status();
            return 0;
        }

        /* Sauvegarder dans NVS */
        nvs_handle_t nvs;
        esp_err_t ret = nvs_open("bmu_config", NVS_READWRITE, &nvs);
        if (ret == ESP_OK) {
            nvs_set_u16(nvs, "min_mv", min_mv);
            nvs_set_u16(nvs, "max_mv", max_mv);
            nvs_set_u16(nvs, "max_ma", max_ma);
            nvs_set_u16(nvs, "diff_mv", diff_mv);
            nvs_commit(nvs);
            nvs_close(nvs);

            ESP_LOGI(TAG, "Config sauvegardee NVS: min=%u max=%u maxI=%u diff=%u mV/mA",
                     min_mv, max_mv, max_ma, diff_mv);
            s_last_status.result = 0;
        } else {
            ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(ret));
            s_last_status.result = 3;
        }
        notify_status();
        return 0;
    }

    case CTRL_CHR_WIFI_CONFIG: {
        if (read_write_payload(ctxt, buf, sizeof(buf), &len) != 0 || len < 2) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        /* SSID max 32 bytes + password max 64 bytes dans un buffer plus grand */
        uint8_t big_buf[96] = {};
        uint16_t big_len = 0;
        ble_hs_mbuf_to_flat(ctxt->om, big_buf, sizeof(big_buf), &big_len);

        char ssid[33] = {};
        char pass[65] = {};
        memcpy(ssid, big_buf, big_len > 32 ? 32 : big_len);
        if (big_len > 32) memcpy(pass, big_buf + 32, big_len - 32 > 64 ? 64 : big_len - 32);

        ESP_LOGI(TAG, "WiFi config via BLE: SSID='%s'", ssid);

        nvs_handle_t nvs;
        if (nvs_open("bmu", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "wifi_ssid", ssid);
            nvs_set_str(nvs, "wifi_pass", pass);
            nvs_commit(nvs);
            nvs_close(nvs);
        }

        s_last_status.last_cmd = 3;
        s_last_status.battery_idx = 0xFF;
        s_last_status.result = 0;
        notify_status();
        return 0;
    }

    case CTRL_CHR_WIFI_STATUS: {
        if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;

        /* Pack: ssid(32) + ip(16) + rssi(1) + connected(1) = 50 bytes */
        uint8_t payload[50] = {};
        bool connected = bmu_wifi_is_connected();
        if (connected) {
            /* Read SSID from NVS */
            nvs_handle_t nvs;
            if (nvs_open("bmu", NVS_READONLY, &nvs) == ESP_OK) {
                size_t ssid_len = 32;
                nvs_get_str(nvs, "wifi_ssid", (char *)payload, &ssid_len);
                nvs_close(nvs);
            }
            bmu_wifi_get_ip((char *)(payload + 32), 16);
            int8_t rssi = 0;
            bmu_wifi_get_rssi(&rssi);
            payload[48] = (uint8_t)rssi;
        }
        payload[49] = connected ? 1 : 0;

        int rc = os_mbuf_append(ctxt->om, payload, sizeof(payload));
        return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    case CTRL_CHR_BAT_LABEL: { /* BAT_LABEL — set battery label */
        uint8_t label_buf[10];
        uint16_t label_buf_len = 0;
        if (read_write_payload(ctxt, label_buf, sizeof(label_buf), &label_buf_len) != 0
            || label_buf_len < 2) {
            ESP_LOGW(TAG, "BAT_LABEL: payload too short (%d)", label_buf_len);
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint8_t idx = label_buf[0];
        if (idx >= BMU_MAX_BATTERIES) {
            s_last_status = {4, idx, 1}; /* invalid idx */
            notify_status();
            return BLE_ATT_ERR_UNLIKELY;
        }
        char label[BMU_CONFIG_BATLABEL_MAX];
        int label_len = (int)label_buf_len - 1;
        if (label_len > (int)(BMU_CONFIG_BATLABEL_MAX - 1))
            label_len = BMU_CONFIG_BATLABEL_MAX - 1;
        memcpy(label, label_buf + 1, label_len);
        label[label_len] = '\0';
        bmu_config_set_battery_label(idx, label);
        bmu_config_save_battery_labels();
        ESP_LOGI(TAG, "BLE BAT_LABEL[%d] = '%s'", idx, label);
        s_last_status = {4, idx, 0};
        notify_status();
        return 0;
    }

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

/* ── Definition du service GATT Control ──────────────────────────── */
static struct ble_gatt_chr_def s_ctrl_chr_defs[] = {
    {
        .uuid       = &s_switch_chr_uuid.u,
        .access_cb  = control_chr_access_cb,
        .arg        = (void *)(intptr_t)CTRL_CHR_SWITCH,
        .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid       = &s_reset_chr_uuid.u,
        .access_cb  = control_chr_access_cb,
        .arg        = (void *)(intptr_t)CTRL_CHR_RESET,
        .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid       = &s_config_chr_uuid.u,
        .access_cb  = control_chr_access_cb,
        .arg        = (void *)(intptr_t)CTRL_CHR_CONFIG,
        .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid       = &s_status_chr_uuid.u,
        .access_cb  = control_chr_access_cb,
        .arg        = (void *)(intptr_t)CTRL_CHR_STATUS,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_status_val_handle,
    },
    /* WiFi Config — write encrypted */
    {
        .uuid       = &s_wifi_cfg_chr_uuid.u,
        .access_cb  = control_chr_access_cb,
        .arg        = (void *)(intptr_t)CTRL_CHR_WIFI_CONFIG,
        .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
    },
    /* WiFi Status — read + notify */
    {
        .uuid       = &s_wifi_sts_chr_uuid.u,
        .access_cb  = control_chr_access_cb,
        .arg        = (void *)(intptr_t)CTRL_CHR_WIFI_STATUS,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_wifi_sts_val_handle,
    },
    /* Battery Label — write encrypted */
    {
        .uuid       = &chr_uuid_bat_label.u,
        .access_cb  = control_chr_access_cb,
        .arg        = (void *)(intptr_t)CTRL_CHR_BAT_LABEL,
        .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
    },
    { 0 }, /* Terminateur */
};

/* ── WiFi status notify timer ─────────────────────────────────────── */
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

static struct ble_gatt_svc_def s_ctrl_svc[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_ctrl_svc_uuid.u,
        .characteristics = s_ctrl_chr_defs,
    },
    { 0 },
};

const struct ble_gatt_svc_def *bmu_ble_control_svc_defs(void)
{
    return s_ctrl_svc;
}

#endif /* CONFIG_BMU_BLE_ENABLED */
