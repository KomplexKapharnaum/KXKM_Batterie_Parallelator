// firmware-idf-v2/components/bmu_ble/src/bmu_ble_gatt.c
//
// Phase 18 : GATT services — Battery (F00D0001), System (F00D0002), Config (F00D0004).
// All characteristics are read-only + notify (Battery/System).

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gatt.h"
#include "os/os_mbuf.h"

#include "bmu_core.h"

static const char *TAG = "bmu-ble-gatt";

/* ---- Core handle for read callbacks ---- */
static struct BmuCore *s_core = NULL;

void bmu_ble_gatt_set_core(struct BmuCore *core) {
    s_core = core;
}

/* ---- Extern peer tracking from bmu_ble.c ---- */
extern uint8_t bmu_ble_get_peer_count(void);
extern const uint16_t *bmu_ble_get_peer_handles(void);

/* ---- UUID macros ---- */
/* Base: F00D????-9B1F-4A6F-9E5D-0A1B2C3D4E5F
 * NimBLE BLE_UUID128_INIT takes bytes in little-endian order. */

#define BMU_UUID128(short_hi, short_lo) \
    BLE_UUID128_INIT(0x5f, 0x4e, 0x3d, 0x2c, 0x1b, 0x0a, 0x5d, 0x9e, \
                     0x6f, 0x4a, 0x1f, 0x9b, (short_lo), (short_hi), 0x0d, 0xf0)

/* Service UUIDs */
static const ble_uuid128_t svc_battery_uuid = BMU_UUID128(0x00, 0x01);  /* F00D0001 */
static const ble_uuid128_t svc_system_uuid  = BMU_UUID128(0x00, 0x02);  /* F00D0002 */
static const ble_uuid128_t svc_config_uuid  = BMU_UUID128(0x00, 0x04);  /* F00D0004 */

/* Battery characteristic UUIDs: F00D0010..F00D001F */
static const ble_uuid128_t chr_bat_uuid[MAX_BATTERIES] = {
    BMU_UUID128(0x00, 0x10), BMU_UUID128(0x00, 0x11),
    BMU_UUID128(0x00, 0x12), BMU_UUID128(0x00, 0x13),
    BMU_UUID128(0x00, 0x14), BMU_UUID128(0x00, 0x15),
    BMU_UUID128(0x00, 0x16), BMU_UUID128(0x00, 0x17),
    BMU_UUID128(0x00, 0x18), BMU_UUID128(0x00, 0x19),
    BMU_UUID128(0x00, 0x1a), BMU_UUID128(0x00, 0x1b),
    BMU_UUID128(0x00, 0x1c), BMU_UUID128(0x00, 0x1d),
    BMU_UUID128(0x00, 0x1e), BMU_UUID128(0x00, 0x1f),
};

/* System characteristic UUID: F00D0020 */
static const ble_uuid128_t chr_sys_uuid = BMU_UUID128(0x00, 0x20);

/* Config characteristic UUID: F00D0040 */
static const ble_uuid128_t chr_cfg_uuid = BMU_UUID128(0x00, 0x40);

/* ---- Val handles for notifications ---- */
static uint16_t s_bat_val_handles[MAX_BATTERIES];
static uint16_t s_sys_val_handle;

/* ---- Read callbacks ---- */

static int on_read_battery(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    uint8_t idx = (uint8_t)(uintptr_t)arg;

    if (s_core == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t buf[24];
    int32_t rc = bmu_core_serialize_battery(s_core, idx, buf);
    if (rc != 0) {
        ESP_LOGW(TAG, "serialize_battery(%u) failed: %ld", idx, (long)rc);
        return BLE_ATT_ERR_UNLIKELY;
    }

    int os_rc = os_mbuf_append(ctxt->om, buf, sizeof(buf));
    return (os_rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int on_read_system(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (s_core == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    struct BmuSnapshotC snap;
    int32_t rc = bmu_core_get_cached_snapshot(s_core, &snap);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Pack BmuSystemC as raw bytes (it's repr(C), memcpy-safe) */
    int os_rc = os_mbuf_append(ctxt->om, &snap.system, sizeof(snap.system));
    return (os_rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int on_read_config(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    /* Pack config as human-readable text string */
    char buf[80];
    /* Use hardcoded values matching main.cpp init_bmu_core */
    int len = snprintf(buf, sizeof(buf),
                       "U:24000-30000 I:1000 V:1000 Sw:5 Rc:10000 Tk:200");

    int os_rc = os_mbuf_append(ctxt->om, buf, (uint16_t)len);
    return (os_rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* ---- GATT service definitions ---- */

/* Battery characteristics (16 entries) — built via macro to keep it DRY */
#define BAT_CHR(i) { \
    .uuid = &chr_bat_uuid[i].u, \
    .access_cb = on_read_battery, \
    .arg = (void *)(uintptr_t)(i), \
    .val_handle = &s_bat_val_handles[i], \
    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, \
}

static const struct ble_gatt_chr_def battery_chars[] = {
    BAT_CHR(0),  BAT_CHR(1),  BAT_CHR(2),  BAT_CHR(3),
    BAT_CHR(4),  BAT_CHR(5),  BAT_CHR(6),  BAT_CHR(7),
    BAT_CHR(8),  BAT_CHR(9),  BAT_CHR(10), BAT_CHR(11),
    BAT_CHR(12), BAT_CHR(13), BAT_CHR(14), BAT_CHR(15),
    { 0 } /* sentinel */
};

static const struct ble_gatt_chr_def system_chars[] = {
    {
        .uuid = &chr_sys_uuid.u,
        .access_cb = on_read_system,
        .arg = NULL,
        .val_handle = &s_sys_val_handle,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    { 0 }
};

static const struct ble_gatt_chr_def config_chars[] = {
    {
        .uuid = &chr_cfg_uuid.u,
        .access_cb = on_read_config,
        .arg = NULL,
        .val_handle = NULL,
        .flags = BLE_GATT_CHR_F_READ,
    },
    { 0 }
};

/* Service table — must be static and persist for lifetime */
static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_battery_uuid.u,
        .characteristics = battery_chars,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_system_uuid.u,
        .characteristics = system_chars,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_config_uuid.u,
        .characteristics = config_chars,
    },
    { 0 } /* sentinel */
};

/* ---- Init ---- */

void bmu_ble_gatt_init(void) {
    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return;
    }

    ESP_LOGI(TAG, "GATT services registered (Battery/System/Config)");
}

/* ---- Notify all ---- */

void bmu_ble_notify_all(struct BmuCore *core) {
    /* Store core for read callbacks */
    s_core = core;

    uint8_t n_peers = bmu_ble_get_peer_count();
    if (n_peers == 0) {
        return;  /* No connected peers, skip */
    }

    const uint16_t *handles = bmu_ble_get_peer_handles();

    /* Get snapshot for n_bat count */
    struct BmuSnapshotC snap;
    int32_t rc = bmu_core_get_cached_snapshot(core, &snap);
    if (rc != 0) {
        return;
    }

    /* Notify battery characteristics */
    for (uint8_t bat = 0; bat < snap.n_bat && bat < MAX_BATTERIES; bat++) {
        uint8_t buf[24];
        rc = bmu_core_serialize_battery(core, bat, buf);
        if (rc != 0) continue;

        for (uint8_t p = 0; p < n_peers; p++) {
            struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, sizeof(buf));
            if (om == NULL) continue;
            ble_gatts_notify_custom(handles[p], s_bat_val_handles[bat], om);
        }
    }

    /* Notify system characteristic */
    for (uint8_t p = 0; p < n_peers; p++) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&snap.system, sizeof(snap.system));
        if (om == NULL) continue;
        ble_gatts_notify_custom(handles[p], s_sys_val_handle, om);
    }
}
