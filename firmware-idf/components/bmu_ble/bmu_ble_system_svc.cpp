/**
 * @file bmu_ble_system_svc.cpp
 * @brief Service GATT System — firmware, heap, uptime, WiFi IP, topology, solar.
 *
 * 6 characteristics (READ, certaines NOTIFY 10s).
 * UUIDs : Service 0x0002, Chars 0x0020..0x0025.
 */
#include "sdkconfig.h"

#if CONFIG_BMU_BLE_ENABLED

#include "bmu_ble_internal.h"
#include "bmu_protection.h"
#include "bmu_vedirect.h"
#include "bmu_wifi.h"
#include "bmu_config.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "os/os_mbuf.h"

#include <cstring>

static const char *TAG = "BLE_SYS";

/* ── Structs packed pour topologie et solar ───────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t nb_ina;
    uint8_t nb_tca;
    uint8_t valid;    /* 1 si nb_tca * 4 == nb_ina */
} ble_topology_char_t;

typedef struct __attribute__((packed)) {
    int16_t  battery_voltage_mv;
    int16_t  battery_current_ma;
    uint16_t panel_voltage_mv;
    uint16_t panel_power_w;
    uint8_t  charge_state;
    uint8_t  error_code;
    uint32_t yield_today_wh;
    uint8_t  valid;
} ble_solar_char_t;

/* ── Value handles pour les notifications ────────────────────────── */
static uint16_t s_heap_val_handle   = 0;
static uint16_t s_solar_val_handle  = 0;
static esp_timer_handle_t s_notify_timer = NULL;

/* ── UUIDs ────────────────────────────────────────────────────────── */
static ble_uuid128_t s_sys_svc_uuid       = BMU_BLE_UUID128_DECLARE(0x02, 0x00);
static ble_uuid128_t s_firmware_chr_uuid   = BMU_BLE_UUID128_DECLARE(0x20, 0x00);
static ble_uuid128_t s_heap_chr_uuid       = BMU_BLE_UUID128_DECLARE(0x21, 0x00);
static ble_uuid128_t s_uptime_chr_uuid     = BMU_BLE_UUID128_DECLARE(0x22, 0x00);
static ble_uuid128_t s_wifi_ip_chr_uuid    = BMU_BLE_UUID128_DECLARE(0x23, 0x00);
static ble_uuid128_t s_topology_chr_uuid   = BMU_BLE_UUID128_DECLARE(0x24, 0x00);
static ble_uuid128_t s_solar_chr_uuid      = BMU_BLE_UUID128_DECLARE(0x25, 0x00);

/* ── Identification de la characteristic par UUID ────────────────── */
enum sys_chr_id {
    SYS_CHR_FIRMWARE = 0,
    SYS_CHR_HEAP,
    SYS_CHR_UPTIME,
    SYS_CHR_WIFI_IP,
    SYS_CHR_TOPOLOGY,
    SYS_CHR_SOLAR,
};

/* ── Callback acces GATT ─────────────────────────────────────────── */
static int system_chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int chr_id = (int)(intptr_t)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    int rc;

    switch (chr_id) {
    case SYS_CHR_FIRMWARE: {
        const char *ver = CONFIG_APP_PROJECT_VER;
        rc = os_mbuf_append(ctxt->om, ver, strlen(ver));
        break;
    }
    case SYS_CHR_HEAP: {
        uint32_t heap = (uint32_t)esp_get_free_heap_size();
        rc = os_mbuf_append(ctxt->om, &heap, sizeof(heap));
        break;
    }
    case SYS_CHR_UPTIME: {
        uint32_t uptime_s = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        rc = os_mbuf_append(ctxt->om, &uptime_s, sizeof(uptime_s));
        break;
    }
    case SYS_CHR_WIFI_IP: {
        char ip[16] = {};
        esp_err_t ret = bmu_wifi_get_ip(ip, sizeof(ip));
        if (ret != ESP_OK || ip[0] == '\0') {
            const char *disc = "disconnected";
            rc = os_mbuf_append(ctxt->om, disc, strlen(disc));
        } else {
            rc = os_mbuf_append(ctxt->om, ip, strlen(ip));
        }
        break;
    }
    case SYS_CHR_TOPOLOGY: {
        bmu_protection_ctx_t *prot = bmu_ble_get_prot();
        ble_topology_char_t topo;
        topo.nb_ina = prot->nb_ina;
        topo.nb_tca = prot->nb_tca;
        topo.valid  = (prot->nb_tca > 0 && prot->nb_tca * 4 == prot->nb_ina) ? 1 : 0;
        rc = os_mbuf_append(ctxt->om, &topo, sizeof(topo));
        break;
    }
    case SYS_CHR_SOLAR: {
        const bmu_vedirect_data_t *vd = bmu_vedirect_get_data();
        ble_solar_char_t solar;
        if (vd && bmu_vedirect_is_connected()) {
            solar.battery_voltage_mv = (int16_t)(vd->battery_voltage_v * 1000.0f);
            solar.battery_current_ma = (int16_t)(vd->battery_current_a * 1000.0f);
            solar.panel_voltage_mv   = (uint16_t)(vd->panel_voltage_v * 1000.0f);
            solar.panel_power_w      = vd->panel_power_w;
            solar.charge_state       = vd->charge_state;
            solar.error_code         = vd->error_code;
            solar.yield_today_wh     = vd->yield_today_wh;
            solar.valid              = 1;
        } else {
            memset(&solar, 0, sizeof(solar));
            solar.valid = 0;
        }
        rc = os_mbuf_append(ctxt->om, &solar, sizeof(solar));
        break;
    }
    default:
        return BLE_ATT_ERR_UNLIKELY;
    }

    return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* ── Timer notification 10s (heap + solar) ───────────────────────── */
static void sys_notify_timer_cb(void *arg)
{
    /* Notify Heap */
    if (s_heap_val_handle != 0) {
        uint32_t heap = (uint32_t)esp_get_free_heap_size();
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&heap, sizeof(heap));
        if (om) {
            ble_gatts_notify_custom(0xFFFF, s_heap_val_handle, om);
        }
    }

    /* Notify Solar */
    if (s_solar_val_handle != 0) {
        const bmu_vedirect_data_t *vd = bmu_vedirect_get_data();
        ble_solar_char_t solar;
        if (vd && bmu_vedirect_is_connected()) {
            solar.battery_voltage_mv = (int16_t)(vd->battery_voltage_v * 1000.0f);
            solar.battery_current_ma = (int16_t)(vd->battery_current_a * 1000.0f);
            solar.panel_voltage_mv   = (uint16_t)(vd->panel_voltage_v * 1000.0f);
            solar.panel_power_w      = vd->panel_power_w;
            solar.charge_state       = vd->charge_state;
            solar.error_code         = vd->error_code;
            solar.yield_today_wh     = vd->yield_today_wh;
            solar.valid              = 1;
        } else {
            memset(&solar, 0, sizeof(solar));
            solar.valid = 0;
        }
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&solar, sizeof(solar));
        if (om) {
            ble_gatts_notify_custom(0xFFFF, s_solar_val_handle, om);
        }
    }
}

void bmu_ble_system_notify_start(void)
{
    if (s_notify_timer) {
        esp_timer_start_periodic(s_notify_timer, 10000000); /* 10s */
        ESP_LOGI(TAG, "System notify timer demarre (10s)");
    }
}

void bmu_ble_system_notify_stop(void)
{
    if (s_notify_timer) {
        esp_timer_stop(s_notify_timer);
        ESP_LOGI(TAG, "System notify timer arrete");
    }
}

/* ── Definition du service GATT System ───────────────────────────── */
static struct ble_gatt_chr_def s_sys_chr_defs[] = {
    {
        .uuid       = &s_firmware_chr_uuid.u,
        .access_cb  = system_chr_access_cb,
        .arg        = (void *)(intptr_t)SYS_CHR_FIRMWARE,
        .flags      = BLE_GATT_CHR_F_READ,
    },
    {
        .uuid       = &s_heap_chr_uuid.u,
        .access_cb  = system_chr_access_cb,
        .arg        = (void *)(intptr_t)SYS_CHR_HEAP,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_heap_val_handle,
    },
    {
        .uuid       = &s_uptime_chr_uuid.u,
        .access_cb  = system_chr_access_cb,
        .arg        = (void *)(intptr_t)SYS_CHR_UPTIME,
        .flags      = BLE_GATT_CHR_F_READ,
    },
    {
        .uuid       = &s_wifi_ip_chr_uuid.u,
        .access_cb  = system_chr_access_cb,
        .arg        = (void *)(intptr_t)SYS_CHR_WIFI_IP,
        .flags      = BLE_GATT_CHR_F_READ,
    },
    {
        .uuid       = &s_topology_chr_uuid.u,
        .access_cb  = system_chr_access_cb,
        .arg        = (void *)(intptr_t)SYS_CHR_TOPOLOGY,
        .flags      = BLE_GATT_CHR_F_READ,
    },
    {
        .uuid       = &s_solar_chr_uuid.u,
        .access_cb  = system_chr_access_cb,
        .arg        = (void *)(intptr_t)SYS_CHR_SOLAR,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_solar_val_handle,
    },
    { 0 }, /* Terminateur */
};

static struct ble_gatt_svc_def s_sys_svc[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_sys_svc_uuid.u,
        .characteristics = s_sys_chr_defs,
    },
    { 0 },
};

static bool s_inited = false;

const struct ble_gatt_svc_def *bmu_ble_system_svc_defs(void)
{
    if (!s_inited) {
        const esp_timer_create_args_t timer_args = {
            .callback = sys_notify_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "ble_sys_notify",
            .skip_unhandled_events = true,
        };
        esp_timer_create(&timer_args, &s_notify_timer);
        s_inited = true;
    }
    return s_sys_svc;
}

#endif /* CONFIG_BMU_BLE_ENABLED */
