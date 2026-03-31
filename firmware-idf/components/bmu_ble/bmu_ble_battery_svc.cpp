/**
 * @file bmu_ble_battery_svc.cpp
 * @brief Service GATT Battery — 16 characteristics (READ + NOTIFY), timer 1s.
 *
 * Chaque batterie est encodee dans une struct packed de 15 octets (integer only).
 * UUIDs : Service 0x0001, Chars 0x0010..0x001F.
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

    /* nb_switch protege par state_mutex */
    if (xSemaphoreTake(prot->state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        out->nb_switch = (uint8_t)(prot->nb_switch[idx] > 255 ? 255 : prot->nb_switch[idx]);
        xSemaphoreGive(prot->state_mutex);
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

/* UUIDs pour chaque characteristic batterie (0x0010..0x001F) */
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
};

/* Tableau de characteristics — construit dynamiquement car arg = index */
static struct ble_gatt_chr_def s_bat_chr_defs[BMU_MAX_BATTERIES + 1]; /* +1 terminateur */

static struct ble_gatt_svc_def s_bat_svc[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_bat_svc_uuid.u,
        .characteristics = s_bat_chr_defs,
    },
    { 0 }, /* Terminateur */
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
        /* Terminateur */
        memset(&s_bat_chr_defs[BMU_MAX_BATTERIES], 0, sizeof(struct ble_gatt_chr_def));

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
