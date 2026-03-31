/**
 * @file bmu_ble.cpp
 * @brief NimBLE BLE init, GAP advertising, pairing/bonding — Phase 9.
 *
 * Initialise le stack NimBLE, configure 3 services GATT (Battery, System, Control),
 * demarre l'advertising BLE connectable avec bonding Secure Connections.
 */
#include "sdkconfig.h"

#if CONFIG_BMU_BLE_ENABLED

#include "bmu_ble.h"
#include "bmu_ble_internal.h"

#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

#include <atomic>

static const char *TAG = "BLE";

/* ── Contexte global (accessible par les fichiers service) ───────── */
static bmu_protection_ctx_t  *s_prot   = NULL;
static bmu_battery_manager_t *s_mgr    = NULL;
static uint8_t                s_nb_ina = 0;
static std::atomic<int>       s_connected_count{0};

bmu_protection_ctx_t  *bmu_ble_get_prot(void)   { return s_prot; }
bmu_battery_manager_t *bmu_ble_get_mgr(void)    { return s_mgr; }
uint8_t                bmu_ble_get_nb_ina(void)  { return s_nb_ina; }

/* ── Forward declarations ────────────────────────────────────────── */
static void start_advertising(void);
static void on_sync(void);
static void on_reset(int reason);
static int  ble_gap_event_handler(struct ble_gap_event *event, void *arg);
static void nimble_host_task(void *param);

/* ── Advertising ─────────────────────────────────────────────────── */
static void start_advertising(void)
{
    /* Advertise uniquement si on n'a pas atteint le max de connexions */
    if (s_connected_count.load() >= CONFIG_BMU_BLE_MAX_CONNECTIONS) {
        ESP_LOGI(TAG, "Max connections atteint (%d), advertising stoppe",
                 CONFIG_BMU_BLE_MAX_CONNECTIONS);
        return;
    }

    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;   /* Connectable undirected */
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;   /* General discoverable */
    adv_params.itvl_min  = BLE_GAP_ADV_ITVL_MS(50); /* 50 ms */
    adv_params.itvl_max  = BLE_GAP_ADV_ITVL_MS(100);/* 100 ms */

    /* Advertising data : flags + nom */
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)CONFIG_BMU_BLE_DEVICE_NAME;
    fields.name_len = strlen(CONFIG_BMU_BLE_DEVICE_NAME);
    fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields: rc=%d", rc);
        return;
    }

    /* Scan response : UUID 128-bit du service Battery */
    struct ble_hs_adv_fields rsp_fields = {};
    static ble_uuid128_t batt_svc_uuid = BMU_BLE_UUID128_DECLARE(0x01, 0x00);
    rsp_fields.uuids128 = &batt_svc_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields: rc=%d", rc);
        /* Pas fatal — on continue sans scan response */
    }

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_handler, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "ble_gap_adv_start: rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising demarre: '%s'", CONFIG_BMU_BLE_DEVICE_NAME);
    }
}

/* ── GAP Event Handler ───────────────────────────────────────────── */
static int ble_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connected_count.fetch_add(1);
            ESP_LOGI(TAG, "Client connecte (conn_handle=%d, total=%d)",
                     event->connect.conn_handle, s_connected_count.load());

            /* Demarrer les timers de notification si c'est le premier client */
            if (s_connected_count.load() == 1) {
                bmu_ble_battery_notify_start();
                bmu_ble_system_notify_start();
            }

            /* Demander connexion securisee */
            ble_gap_security_initiate(event->connect.conn_handle);

            /* Reprendre l'advertising si places restantes */
            start_advertising();
        } else {
            ESP_LOGW(TAG, "Connexion echouee: status=%d", event->connect.status);
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_connected_count.fetch_sub(1);
        ESP_LOGI(TAG, "Client deconnecte (reason=0x%02x, total=%d)",
                 event->disconnect.reason, s_connected_count.load());

        /* Stopper les timers si plus aucun client */
        if (s_connected_count.load() <= 0) {
            s_connected_count.store(0);
            bmu_ble_battery_notify_stop();
            bmu_ble_system_notify_stop();
        }

        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGD(TAG, "Advertising termine — redemarrage");
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "Encryption changee: conn_handle=%d status=%d",
                 event->enc_change.conn_handle, event->enc_change.status);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* Supprimer l'ancien bond et accepter le nouveau pairing */
        struct ble_gap_conn_desc desc;
        ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        ESP_LOGI(TAG, "Re-pairing accepte pour conn_handle=%d",
                 event->repeat_pairing.conn_handle);
        return 0; /* 0 = accepter le nouveau pairing */
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGD(TAG, "Subscribe: conn=%d attr=%d cur_notify=%d",
                 event->subscribe.conn_handle,
                 event->subscribe.attr_handle,
                 event->subscribe.cur_notify);
        return 0;

    default:
        return 0;
    }
}

/* ── Callbacks host sync/reset ───────────────────────────────────── */
static void on_sync(void)
{
    /* S'assurer qu'on a une adresse valide */
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr: rc=%d", rc);
        return;
    }

    uint8_t addr[6] = {};
    uint8_t addr_type = 0;
    rc = ble_hs_id_infer_auto(0, &addr_type);
    if (rc == 0) {
        ble_hs_id_copy_addr(addr_type, addr, NULL);
        ESP_LOGI(TAG, "Adresse BLE: %02x:%02x:%02x:%02x:%02x:%02x (type=%d)",
                 addr[5], addr[4], addr[3], addr[2], addr[1], addr[0], addr_type);
    }

    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "NimBLE host reset: reason=%d", reason);
}

/* ── NimBLE host task ────────────────────────────────────────────── */
static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task demarree");
    nimble_port_run();              /* Bloque jusqu'a arret */
    nimble_port_freertos_deinit();
}

/* ══════════════════════════════════════════════════════════════════ */
/* ── API publique ─────────────────────────────────────────────────── */
/* ══════════════════════════════════════════════════════════════════ */

esp_err_t bmu_ble_init(bmu_protection_ctx_t *prot,
                        bmu_battery_manager_t *mgr,
                        uint8_t nb_ina)
{
    if (!prot || !mgr || nb_ina == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    s_prot   = prot;
    s_mgr    = mgr;
    s_nb_ina = nb_ina;

    ESP_LOGI(TAG, "Init NimBLE BLE — %d batteries, max %d connexions",
             nb_ina, CONFIG_BMU_BLE_MAX_CONNECTIONS);

    /* 1. Initialiser le stack NimBLE */
    int rc = nimble_port_init();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init: rc=%d", rc);
        return ESP_FAIL;
    }

    /* 2. Configurer le host */
    ble_hs_cfg.reset_cb  = on_reset;
    ble_hs_cfg.sync_cb   = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* 3. Security Manager — bonding + Secure Connections */
    ble_hs_cfg.sm_io_cap        = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding       = 1;
    ble_hs_cfg.sm_sc            = 1;  /* Secure Connections */
    ble_hs_cfg.sm_our_key_dist  = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    /* 4. Configurer le nom GAP */
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(CONFIG_BMU_BLE_DEVICE_NAME);

    /* 5. Construire et enregistrer la table GATT unifiee */
    /* NimBLE attend un tableau termine par un element {0} */
    static struct ble_gatt_svc_def gatt_svcs[4]; /* 3 services + terminateur */

    const struct ble_gatt_svc_def *batt_svc = bmu_ble_battery_svc_defs();
    const struct ble_gatt_svc_def *sys_svc  = bmu_ble_system_svc_defs();
    const struct ble_gatt_svc_def *ctrl_svc = bmu_ble_control_svc_defs();

    gatt_svcs[0] = batt_svc[0];
    gatt_svcs[1] = sys_svc[0];
    gatt_svcs[2] = ctrl_svc[0];
    memset(&gatt_svcs[3], 0, sizeof(struct ble_gatt_svc_def));

    rc = ble_gatts_count_cfg(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg: rc=%d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs: rc=%d", rc);
        return ESP_FAIL;
    }

    /* 6. Demarrer le host NimBLE dans une tache FreeRTOS */
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "NimBLE initialise — services GATT enregistres");
    return ESP_OK;
}

bool bmu_ble_is_connected(void)
{
    return s_connected_count.load() > 0;
}

int bmu_ble_connected_count(void)
{
    return s_connected_count.load();
}

#endif /* CONFIG_BMU_BLE_ENABLED */
