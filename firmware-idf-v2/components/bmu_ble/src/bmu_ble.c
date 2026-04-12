// firmware-idf-v2/components/bmu_ble/src/bmu_ble.c
//
// Phase 18 : NimBLE init + GAP advertising pour BMU BLE read-only.

#include "bmu_ble.h"

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "bmu-ble";

/* ---- Forward declarations (bmu_ble_gatt.c) ---- */
extern void bmu_ble_gatt_init(void);

/* ---- Connected peers tracking (max 4) ---- */
#define BLE_MAX_PEERS 4

static uint16_t s_peer_conn_handles[BLE_MAX_PEERS];
static uint8_t  s_peer_count = 0;

uint8_t bmu_ble_get_peer_count(void) {
    return s_peer_count;
}

const uint16_t *bmu_ble_get_peer_handles(void) {
    return s_peer_conn_handles;
}

/* ---- Forward declarations (local) ---- */
static int ble_gap_event_handler(struct ble_gap_event *event, void *arg);

/* ---- Advertising ---- */

static void start_advertising(void) {
    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;  /* connectable undirected */
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;  /* general discoverable */

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 0;
    fields.name = (const uint8_t *)ble_svc_gap_device_name();
    fields.name_len = strlen(ble_svc_gap_device_name());
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_handler, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "BLE advertising started");
    }
}

/* ---- GAP event handler ---- */

static int ble_gap_event_handler(struct ble_gap_event *event, void *arg) {
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            uint16_t h = event->connect.conn_handle;
            ESP_LOGI(TAG, "BLE peer connected: conn_handle=%u", h);
            if (s_peer_count < BLE_MAX_PEERS) {
                s_peer_conn_handles[s_peer_count++] = h;
            } else {
                ESP_LOGW(TAG, "Max BLE peers reached, ignoring");
            }
        } else {
            ESP_LOGW(TAG, "BLE connect failed: status=%d", event->connect.status);
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE peer disconnected: conn_handle=%u reason=%d",
                 event->disconnect.conn.conn_handle,
                 event->disconnect.reason);
        /* Remove peer from list */
        {
            uint16_t h = event->disconnect.conn.conn_handle;
            for (int i = 0; i < s_peer_count; i++) {
                if (s_peer_conn_handles[i] == h) {
                    s_peer_conn_handles[i] = s_peer_conn_handles[s_peer_count - 1];
                    s_peer_count--;
                    break;
                }
            }
        }
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "BLE adv complete, restarting");
        start_advertising();
        break;

    default:
        break;
    }
    return 0;
}

/* ---- NimBLE host task ---- */

static void nimble_host_task(void *param) {
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();          /* blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

/* ---- Sync callback ---- */

static void ble_on_sync(void) {
    ESP_LOGI(TAG, "NimBLE host synced");
    start_advertising();
}

static void ble_on_reset(int reason) {
    ESP_LOGW(TAG, "NimBLE host reset: reason=%d", reason);
}

/* ---- Public API ---- */

esp_err_t bmu_ble_init(void) {
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Configure NimBLE host */
    ble_hs_cfg.sync_cb         = ble_on_sync;
    ble_hs_cfg.reset_cb        = ble_on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* GAP + GATT mandatory services */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* Device name with MAC suffix */
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_BT);
        char name[20];
        snprintf(name, sizeof(name), "KXKM-BMU-%02X%02X", mac[4], mac[5]);
        ble_svc_gap_device_name_set(name);
        ESP_LOGI(TAG, "BLE device name: %s", name);
    }

    /* Register application GATT services */
    bmu_ble_gatt_init();

    /* Start NimBLE host task on its own FreeRTOS task */
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE init complete");
    return ESP_OK;
}
