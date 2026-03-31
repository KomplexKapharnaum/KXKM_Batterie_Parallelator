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

#include "esp_log.h"
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

enum ctrl_chr_id {
    CTRL_CHR_SWITCH = 0,
    CTRL_CHR_RESET,
    CTRL_CHR_CONFIG,
    CTRL_CHR_STATUS,
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
        .val_handle = &s_status_val_handle,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    { 0 }, /* Terminateur */
};

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
