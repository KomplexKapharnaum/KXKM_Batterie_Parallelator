// firmware-idf-v2/components/bmu_ble/src/bmu_ble_wifi_prov.c
//
// Phase 20 : Wi-Fi provisioning via BLE command 0x08.
//
// Extended frame layout (138 bytes) :
//   byte 0:        cmd_id (0x08)
//   byte 1:        flags (0x01 = v2 extended)
//   byte 2-33:     ssid[32] (nul-padded)
//   byte 34-97:    psk[64] (nul-padded)
//   byte 98-105:   nonce (u64 BE)
//   byte 106-137:  hmac[32] (HMAC-SHA256 of bytes 0..105)

#include <string.h>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "os/os_mbuf.h"

#include "bmu_ble_wifi_prov.h"
#include "bmu_ble_hmac.h"
#include "bmu_ble_audit.h"
#include "bmu_wifi.h"

static const char *TAG = "bmu-ble-wprov";

#define WIFI_PROV_FRAME_SIZE  138
#define WIFI_PROV_CMD_ID      0x08
#define WIFI_PROV_SIGNED_LEN  106  /* bytes 0..105 */
#define WIFI_PROV_HMAC_LEN    32

/* Packed frame for parsing */
typedef struct __attribute__((packed)) {
    uint8_t  cmd_id;
    uint8_t  flags;
    char     ssid[32];
    char     psk[64];
    uint8_t  nonce_be[8];
    uint8_t  hmac[32];
} wifi_prov_frame_t;

_Static_assert(sizeof(wifi_prov_frame_t) == WIFI_PROV_FRAME_SIZE,
               "wifi_prov_frame_t must be 138 bytes");

/* ---- Delayed reboot task ---- */

static void reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGW(TAG, "WifiProv: rebooting now");
    esp_restart();
}

/* ---- Helpers ---- */

static uint64_t read_u64_be(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = (v << 8) | (uint64_t)p[i];
    }
    return v;
}

/* ---- Main handler ---- */

int bmu_ble_wifi_prov_handle(uint16_t conn_handle, struct ble_gatt_access_ctxt *ctxt)
{
    /* 1. Check encrypted + bonded */
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGW(TAG, "conn_find failed: %d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (!desc.sec_state.encrypted || !desc.sec_state.bonded) {
        ESP_LOGW(TAG, "write rejected: not encrypted/bonded");
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, WIFI_PROV_CMD_ID, "not_encrypted");
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    /* 2. Check frame size */
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len != WIFI_PROV_FRAME_SIZE) {
        ESP_LOGW(TAG, "bad frame size: %u (expected %d)", len, WIFI_PROV_FRAME_SIZE);
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, WIFI_PROV_CMD_ID, "bad_frame_size");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    /* 3. Flatten mbuf */
    wifi_prov_frame_t frame;
    rc = ble_hs_mbuf_to_flat(ctxt->om, &frame, sizeof(frame), &len);
    if (rc != 0) {
        ESP_LOGE(TAG, "mbuf_to_flat failed: %d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* 4. HMAC verify (first 106 bytes signed, 32-byte full tag) */
    if (!bmu_ble_hmac_verify(conn_handle, &frame, WIFI_PROV_SIGNED_LEN,
                             frame.hmac, WIFI_PROV_HMAC_LEN)) {
        ESP_LOGW(TAG, "HMAC verify failed for conn %u", conn_handle);
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, WIFI_PROV_CMD_ID, "hmac_fail");
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    /* 5. Anti-replay: nonce must be strictly greater than last seen */
    uint64_t nonce = read_u64_be(frame.nonce_be);
    uint64_t last_nonce = bmu_ble_hmac_load_nonce(&desc.peer_ota_addr);
    if (nonce <= last_nonce) {
        ESP_LOGW(TAG, "anti-replay: nonce %" PRIu64 " <= %" PRIu64, nonce, last_nonce);
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, WIFI_PROV_CMD_ID, "replay");
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* 6. Persist nonce BEFORE side effects */
    bmu_ble_hmac_persist_nonce(&desc.peer_ota_addr, nonce);

    /* 7. Enforce nul-termination */
    frame.ssid[31] = '\0';
    frame.psk[63] = '\0';

    /* 8. Validate ssid not empty */
    if (frame.ssid[0] == '\0') {
        ESP_LOGW(TAG, "empty SSID");
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, WIFI_PROV_CMD_ID, "empty_ssid");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    /* 9. Log WITHOUT revealing PSK */
    ESP_LOGI(TAG, "WifiProv: ssid='%s' psk_len=%d", frame.ssid, (int)strlen(frame.psk));

    /* 10. Write credentials to NVS */
    esp_err_t err = bmu_wifi_set_creds(frame.ssid, frame.psk);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmu_wifi_set_creds failed: %s", esp_err_to_name(err));
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, WIFI_PROV_CMD_ID, "nvs_fail");
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* 11. Audit log PASS */
    bmu_ble_audit_log_pass(&desc.peer_ota_addr, WIFI_PROV_CMD_ID, 0, 0, 0);

    /* 12. Schedule delayed reboot */
    xTaskCreate(reboot_task, "wprov_reboot", 2048, NULL, 1, NULL);

    return 0;
}
