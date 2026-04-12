// firmware-idf-v2/components/bmu_ble_victron/src/bmu_ble_victron.c
//
// Phase 21 : Victron SmartShunt GATT service 0x6597 — 5 characteristics.
// Voltage, Current, SoC, Power, Consumed Ah — all READ + NOTIFY.

#include "bmu_ble_victron.h"
#include "fleet_agg.h"
#include "bmu_core.h"

#include <string.h>
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "bmu-victron";

/* ---- External from bmu_ble_gatt.c (Phase 19) ---- */
extern struct BmuCore *bmu_ble_gatt_get_core(void);

/* ---- Victron UUIDs (16-bit) ---- */
#define VICTRON_SVC_UUID        0x6597
#define VICTRON_CHR_VOLTAGE     0x6598
#define VICTRON_CHR_CURRENT     0x6599
#define VICTRON_CHR_SOC         0x659A
#define VICTRON_CHR_POWER       0x659B
#define VICTRON_CHR_CONSUMED_AH 0x659C

/* ---- Characteristic value handles (for notifications) ---- */
static uint16_t s_hdl_voltage;
static uint16_t s_hdl_current;
static uint16_t s_hdl_soc;
static uint16_t s_hdl_power;
static uint16_t s_hdl_consumed_ah;

/* ---- Helper: compute fleet agg from core snapshot ---- */
static int get_fleet_agg(bmu_fleet_agg_t *agg) {
    struct BmuCore *core = bmu_ble_gatt_get_core();
    if (!core) return -1;

    struct BmuSnapshotC snap;
    int32_t rc = bmu_core_get_cached_snapshot(core, &snap);
    if (rc != 0) return -1;

    fleet_agg_compute(&snap, agg);
    return 0;
}

/* ---- GATT access callback ---- */
static int victron_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    bmu_fleet_agg_t agg;
    if (get_fleet_agg(&agg) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (attr_handle == s_hdl_voltage) {
        uint16_t v = agg.voltage_mv;
        return os_mbuf_append(ctxt->om, &v, sizeof(v));
    } else if (attr_handle == s_hdl_current) {
        int32_t i = agg.current_ma;
        return os_mbuf_append(ctxt->om, &i, sizeof(i));
    } else if (attr_handle == s_hdl_soc) {
        uint8_t s = agg.soc_pct;
        return os_mbuf_append(ctxt->om, &s, sizeof(s));
    } else if (attr_handle == s_hdl_power) {
        int32_t p = agg.power_cw;
        return os_mbuf_append(ctxt->om, &p, sizeof(p));
    } else if (attr_handle == s_hdl_consumed_ah) {
        uint32_t c = (uint32_t)agg.consumed_ah_mah;
        return os_mbuf_append(ctxt->om, &c, sizeof(c));
    }

    return BLE_ATT_ERR_UNLIKELY;
}

/* ---- Static GATT service definition (must persist in memory) ---- */
static const struct ble_gatt_svc_def s_victron_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(VICTRON_SVC_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(VICTRON_CHR_VOLTAGE),
                .access_cb = victron_chr_access,
                .val_handle = &s_hdl_voltage,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = BLE_UUID16_DECLARE(VICTRON_CHR_CURRENT),
                .access_cb = victron_chr_access,
                .val_handle = &s_hdl_current,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = BLE_UUID16_DECLARE(VICTRON_CHR_SOC),
                .access_cb = victron_chr_access,
                .val_handle = &s_hdl_soc,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = BLE_UUID16_DECLARE(VICTRON_CHR_POWER),
                .access_cb = victron_chr_access,
                .val_handle = &s_hdl_power,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                .uuid = BLE_UUID16_DECLARE(VICTRON_CHR_CONSUMED_AH),
                .access_cb = victron_chr_access,
                .val_handle = &s_hdl_consumed_ah,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 },  /* terminator */
        },
    },
    { 0 },  /* terminator */
};

/* ---- Public API ---- */

int bmu_ble_victron_gatt_init(void) {
    int rc = ble_gatts_count_cfg(s_victron_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(s_victron_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return rc;
    }

    ESP_LOGI(TAG, "Victron GATT service 0x%04X registered", VICTRON_SVC_UUID);
    return 0;
}
