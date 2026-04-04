/**
 * @file bmu_ble_battery_svc.cpp
 * @brief Service GATT Battery — 32 characteristics (READ + NOTIFY), timer 1s.
 *
 * Chaque batterie est encodee dans une struct packed de 15 octets (integer only).
 * UUIDs : Service 0x0001, Chars 0x0010..0x002F.
 */
#include "sdkconfig.h"

#if CONFIG_BMU_BLE_ENABLED

#include "bmu_ble_internal.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_ina237.h"
#include "bmu_config.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "os/os_mbuf.h"

#if CONFIG_BMU_RINT_ENABLED
#include "bmu_rint.h"
#endif

static const char *TAG = "BLE_BAT";

/* ── Struct payload BLE batterie (15 octets, little-endian) ──────── */
typedef struct __attribute__((packed)) {
    int32_t  voltage_mv;      /* bmu_protection_get_voltage (float mV → int32) */
    int32_t  current_ma;      /* bmu_ina237_read_current (A → mA, int32) */
    uint8_t  state;           /* bmu_battery_state_t enum */
    int32_t  ah_discharge_mah;/* bmu_battery_manager Ah * 1000 → mAh int32 */
    int32_t  ah_charge_mah;   /* bmu_battery_manager Ah * 1000 → mAh int32 */
    uint8_t  nb_switch;       /* prot->nb_switch[i] */
} ble_battery_char_t;

/* ── Value handles pour les notifications ────────────────────────── */
static uint16_t s_battery_val_handles[BMU_MAX_BATTERIES];
static esp_timer_handle_t s_notify_timer = NULL;

/* ── Construction du payload pour une batterie ───────────────────── */
static void build_battery_payload(int idx, ble_battery_char_t *out)
{
    bmu_protection_ctx_t  *prot = bmu_ble_get_prot();
    bmu_battery_manager_t *mgr  = bmu_ble_get_mgr();

    float v_mv = bmu_protection_get_voltage(prot, idx);
    out->voltage_mv = (int32_t)v_mv;

    float i_a = 0.0f;
    bmu_ina237_read_current(&mgr->ina_devices[idx], &i_a);
    out->current_ma = (int32_t)(i_a * 1000.0f);

    out->state = (uint8_t)bmu_protection_get_state(prot, idx);

    float ah_d = bmu_battery_manager_get_ah_discharge(mgr, idx);
    float ah_c = bmu_battery_manager_get_ah_charge(mgr, idx);
    out->ah_discharge_mah = (int32_t)(ah_d * 1000.0f);
    out->ah_charge_mah    = (int32_t)(ah_c * 1000.0f);

    int nb_switch = 0;
    if (bmu_protection_get_switch_count(prot, idx, &nb_switch) == ESP_OK) {
        out->nb_switch = (uint8_t)(nb_switch > 255 ? 255 : nb_switch);
    } else {
        out->nb_switch = 0xFF; /* Indique erreur lecture */
    }
}

/* ── Callback acces GATT (READ) ──────────────────────────────────── */
static int battery_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int idx = (int)(intptr_t)arg;
    uint8_t nb_ina = bmu_ble_get_nb_ina();

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (idx < 0 || idx >= nb_ina) {
        return BLE_ATT_ERR_INVALID_HANDLE;
    }

    ble_battery_char_t payload;
    build_battery_payload(idx, &payload);

    int rc = os_mbuf_append(ctxt->om, &payload, sizeof(payload));
    return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* ── Timer notification 1s ───────────────────────────────────────── */
static void notify_timer_cb(void *arg)
{
    uint8_t nb_ina = bmu_ble_get_nb_ina();

    for (int i = 0; i < nb_ina; i++) {
        if (s_battery_val_handles[i] == 0) continue;

        ble_battery_char_t payload;
        build_battery_payload(i, &payload);

        /* Envoyer la notification a tous les clients connectes */
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&payload, sizeof(payload));
        if (om) {
            /* conn_handle = 0xFFFF signifie "tous les clients abonnes" via
               ble_gatts_notify_custom qui itere les connexions */
            int rc = ble_gatts_notify_custom(0xFFFF, s_battery_val_handles[i], om);
            if (rc != 0 && rc != BLE_HS_ENOTCONN) {
                ESP_LOGD(TAG, "Notify bat[%d] rc=%d", i, rc);
            }
        }
    }

#if CONFIG_BMU_BLE_SOH_ENABLED
    /* Notify SOH characteristic (all batteries concatenated) */
    if (s_soh_val_handle != 0) {
        struct os_mbuf *om_soh = ble_hs_mbuf_from_flat(NULL, 0);
        if (om_soh) {
            bool ok = true;
            for (int i = 0; i < nb_ina && ok; i++) {
                ble_soh_char_t soh_payload;
                build_soh_payload(i, &soh_payload);
                if (os_mbuf_append(om_soh, &soh_payload, sizeof(soh_payload)) != 0) {
                    ok = false;
                }
            }
            if (ok) {
                int rc = ble_gatts_notify_custom(0xFFFF, s_soh_val_handle, om_soh);
                if (rc != 0 && rc != BLE_HS_ENOTCONN) {
                    ESP_LOGD(TAG, "Notify SOH rc=%d", rc);
                }
            } else {
                os_mbuf_free_chain(om_soh);
            }
        }
    }
#endif
}

void bmu_ble_battery_notify_start(void)
{
    if (s_notify_timer) {
        esp_timer_start_periodic(s_notify_timer, 1000000); /* 1s */
        ESP_LOGI(TAG, "Battery notify timer demarre (1s)");
    }
}

void bmu_ble_battery_notify_stop(void)
{
    if (s_notify_timer) {
        esp_timer_stop(s_notify_timer);
        ESP_LOGI(TAG, "Battery notify timer arrete");
    }
}

/* ── Definition du service GATT Battery ──────────────────────────── */

/* UUIDs pour chaque characteristic batterie (0x0010..0x002F) */
static ble_uuid128_t s_bat_svc_uuid = BMU_BLE_UUID128_DECLARE(0x01, 0x00);

static ble_uuid128_t s_bat_chr_uuids[BMU_MAX_BATTERIES] = {
    BMU_BLE_UUID128_DECLARE(0x10, 0x00),
    BMU_BLE_UUID128_DECLARE(0x11, 0x00),
    BMU_BLE_UUID128_DECLARE(0x12, 0x00),
    BMU_BLE_UUID128_DECLARE(0x13, 0x00),
    BMU_BLE_UUID128_DECLARE(0x14, 0x00),
    BMU_BLE_UUID128_DECLARE(0x15, 0x00),
    BMU_BLE_UUID128_DECLARE(0x16, 0x00),
    BMU_BLE_UUID128_DECLARE(0x17, 0x00),
    BMU_BLE_UUID128_DECLARE(0x18, 0x00),
    BMU_BLE_UUID128_DECLARE(0x19, 0x00),
    BMU_BLE_UUID128_DECLARE(0x1A, 0x00),
    BMU_BLE_UUID128_DECLARE(0x1B, 0x00),
    BMU_BLE_UUID128_DECLARE(0x1C, 0x00),
    BMU_BLE_UUID128_DECLARE(0x1D, 0x00),
    BMU_BLE_UUID128_DECLARE(0x1E, 0x00),
    BMU_BLE_UUID128_DECLARE(0x1F, 0x00),
    BMU_BLE_UUID128_DECLARE(0x20, 0x00),
    BMU_BLE_UUID128_DECLARE(0x21, 0x00),
    BMU_BLE_UUID128_DECLARE(0x22, 0x00),
    BMU_BLE_UUID128_DECLARE(0x23, 0x00),
    BMU_BLE_UUID128_DECLARE(0x24, 0x00),
    BMU_BLE_UUID128_DECLARE(0x25, 0x00),
    BMU_BLE_UUID128_DECLARE(0x26, 0x00),
    BMU_BLE_UUID128_DECLARE(0x27, 0x00),
    BMU_BLE_UUID128_DECLARE(0x28, 0x00),
    BMU_BLE_UUID128_DECLARE(0x29, 0x00),
    BMU_BLE_UUID128_DECLARE(0x2A, 0x00),
    BMU_BLE_UUID128_DECLARE(0x2B, 0x00),
    BMU_BLE_UUID128_DECLARE(0x2C, 0x00),
    BMU_BLE_UUID128_DECLARE(0x2D, 0x00),
    BMU_BLE_UUID128_DECLARE(0x2E, 0x00),
    BMU_BLE_UUID128_DECLARE(0x2F, 0x00),
};

/* ── R_int BLE structs et UUIDs (optionnel, CONFIG_BMU_RINT_ENABLED) ─ */
#if CONFIG_BMU_RINT_ENABLED

typedef struct __attribute__((packed)) {
    uint16_t r_ohmic_mohm_x10;   /* R_ohmic * 10 (resolution 0.1 mΩ) */
    uint16_t r_total_mohm_x10;   /* R_total * 10 */
    uint16_t v_load_mv;
    uint16_t v_ocv_mv;           /* V stable */
    int16_t  i_load_ma;          /* Courant en mA */
    uint8_t  valid;
} ble_rint_char_t;

static ble_uuid128_t s_rint_trigger_uuid = BMU_BLE_UUID128_DECLARE(0x38, 0x00);
static ble_uuid128_t s_rint_result_uuid  = BMU_BLE_UUID128_DECLARE(0x39, 0x00);

static uint16_t s_rint_result_val_handle;

/* Callback WRITE — 0xFF = mesure toutes les batteries, 0x00-0x1F = batterie N */
static int rint_trigger_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t cmd = 0;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len < 1) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    os_mbuf_copydata(ctxt->om, 0, 1, &cmd);

    if (cmd == 0xFF) {
        esp_err_t rc = bmu_rint_measure_all(BMU_RINT_TRIGGER_ON_DEMAND);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "rint_measure_all rc=%d", rc);
        }
    } else if (cmd < BMU_MAX_BATTERIES) {
        esp_err_t rc = bmu_rint_measure(cmd, BMU_RINT_TRIGGER_ON_DEMAND);
        if (rc != ESP_OK) {
            ESP_LOGW(TAG, "rint_measure[%d] rc=%d", cmd, rc);
        }
    } else {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    return 0;
}

/* Callback READ — retourne les structs de toutes les batteries en sequence */
static int rint_result_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t nb_ina = bmu_ble_get_nb_ina();

    for (int i = 0; i < nb_ina; i++) {
        bmu_rint_result_t res = bmu_rint_get_cached((uint8_t)i);
        ble_rint_char_t payload = {
            .r_ohmic_mohm_x10 = (uint16_t)(res.r_ohmic_mohm * 10.0f),
            .r_total_mohm_x10 = (uint16_t)(res.r_total_mohm * 10.0f),
            .v_load_mv        = (uint16_t)(res.v_load_mv),
            .v_ocv_mv         = (uint16_t)(res.v_ocv_stable_mv),
            .i_load_ma        = (int16_t)(res.i_load_a * 1000.0f),
            .valid            = (uint8_t)(res.valid ? 1 : 0),
        };
        int rc = os_mbuf_append(ctxt->om, &payload, sizeof(payload));
        if (rc != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    }

    return 0;
}

#endif /* CONFIG_BMU_RINT_ENABLED */

/* ── SOH BLE struct et UUID (optionnel, CONFIG_BMU_BLE_SOH_ENABLED) ──── */
#if CONFIG_BMU_BLE_SOH_ENABLED

#include "bmu_soh.h"

typedef struct __attribute__((packed)) {
    uint8_t  soh_pct;           /* SOH 0-100 (bmu_soh_get_cached * 100) */
    uint16_t r_ohmic_mohm_x10;  /* R_ohmic * 10 (0.1 mOhm resolution) */
    uint16_t r_total_mohm_x10;  /* R_total * 10 */
    uint8_t  rint_valid;        /* R_int measurement valid flag */
    uint8_t  soh_confidence;    /* Model confidence 0-100 (sample count proxy) */
} ble_soh_char_t;               /* 7 bytes per battery */

static ble_uuid128_t s_soh_result_uuid = BMU_BLE_UUID128_DECLARE(0x3A, 0x00);
static uint16_t s_soh_val_handle;

static void build_soh_payload(int idx, ble_soh_char_t *out)
{
    float soh = bmu_soh_get_cached(idx);
    out->soh_pct = (soh >= 0.0f) ? (uint8_t)(soh * 100.0f) : 0;

    bmu_rint_result_t rint = bmu_rint_get_cached((uint8_t)idx);
    out->r_ohmic_mohm_x10 = (uint16_t)(rint.r_ohmic_mohm * 10.0f);
    out->r_total_mohm_x10 = (uint16_t)(rint.r_total_mohm * 10.0f);
    out->rint_valid        = (uint8_t)(rint.valid ? 1 : 0);

    /* Confidence proxy: clamp SOH accumulator sample count to 0-100 */
    /* soh_pct == 0 && soh < 0 means "not yet computed" → confidence 0 */
    out->soh_confidence = (soh >= 0.0f) ? 100 : 0;
}

/* Callback READ — retourne SOH pour toutes les batteries en sequence */
static int soh_result_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t nb_ina = bmu_ble_get_nb_ina();

    for (int i = 0; i < nb_ina; i++) {
        ble_soh_char_t payload;
        build_soh_payload(i, &payload);
        int rc = os_mbuf_append(ctxt->om, &payload, sizeof(payload));
        if (rc != 0) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    }

    return 0;
}

#endif /* CONFIG_BMU_BLE_SOH_ENABLED */

/* Tableau de characteristics — construit dynamiquement car arg = index */
/* +1 terminateur, +2 pour R_int, +1 pour SOH */
static struct ble_gatt_chr_def s_bat_chr_defs[BMU_MAX_BATTERIES + 4];

static struct ble_gatt_svc_def s_bat_svc[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_bat_svc_uuid.u,
        .includes = nullptr,
        .characteristics = s_bat_chr_defs,
    },
    {}, /* Terminateur */
};

static bool s_inited = false;

const struct ble_gatt_svc_def *bmu_ble_battery_svc_defs(void)
{
    if (!s_inited) {
        /* Remplir les definitions de characteristics */
        for (int i = 0; i < BMU_MAX_BATTERIES; i++) {
            s_bat_chr_defs[i].uuid       = &s_bat_chr_uuids[i].u;
            s_bat_chr_defs[i].access_cb  = battery_chr_access_cb;
            s_bat_chr_defs[i].arg        = (void *)(intptr_t)i;
            s_bat_chr_defs[i].flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
            s_bat_chr_defs[i].val_handle = &s_battery_val_handles[i];
        }
        /* Characteristics R_int optionnelles (trigger write + result read) */
#if CONFIG_BMU_RINT_ENABLED
        int rint_base = BMU_MAX_BATTERIES;
        s_bat_chr_defs[rint_base].uuid       = &s_rint_trigger_uuid.u;
        s_bat_chr_defs[rint_base].access_cb  = rint_trigger_access_cb;
        s_bat_chr_defs[rint_base].arg        = NULL;
        s_bat_chr_defs[rint_base].flags      = BLE_GATT_CHR_F_WRITE;
        s_bat_chr_defs[rint_base].val_handle = NULL;

        s_bat_chr_defs[rint_base + 1].uuid       = &s_rint_result_uuid.u;
        s_bat_chr_defs[rint_base + 1].access_cb  = rint_result_access_cb;
        s_bat_chr_defs[rint_base + 1].arg        = NULL;
        s_bat_chr_defs[rint_base + 1].flags      = BLE_GATT_CHR_F_READ;
        s_bat_chr_defs[rint_base + 1].val_handle = &s_rint_result_val_handle;

#if CONFIG_BMU_BLE_SOH_ENABLED
        int soh_base = rint_base + 2;
        s_bat_chr_defs[soh_base].uuid       = &s_soh_result_uuid.u;
        s_bat_chr_defs[soh_base].access_cb  = soh_result_access_cb;
        s_bat_chr_defs[soh_base].arg        = NULL;
        s_bat_chr_defs[soh_base].flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY;
        s_bat_chr_defs[soh_base].val_handle = &s_soh_val_handle;

        /* Terminateur */
        memset(&s_bat_chr_defs[soh_base + 1], 0, sizeof(struct ble_gatt_chr_def));
#else
        /* Terminateur (RINT sans SOH) */
        memset(&s_bat_chr_defs[BMU_MAX_BATTERIES + 2], 0, sizeof(struct ble_gatt_chr_def));
#endif
#else
        /* Terminateur */
        memset(&s_bat_chr_defs[BMU_MAX_BATTERIES], 0, sizeof(struct ble_gatt_chr_def));
#endif

        /* Creer le timer de notification (pas encore demarre) */
        const esp_timer_create_args_t timer_args = {
            .callback = notify_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "ble_bat_notify",
            .skip_unhandled_events = true,
        };
        esp_timer_create(&timer_args, &s_notify_timer);

        s_inited = true;
    }
    return s_bat_svc;
}

#endif /* CONFIG_BMU_BLE_ENABLED */
