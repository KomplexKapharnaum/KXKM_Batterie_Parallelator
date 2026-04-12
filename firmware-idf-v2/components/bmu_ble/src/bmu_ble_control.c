// firmware-idf-v2/components/bmu_ble/src/bmu_ble_control.c
//
// Phase 19 : Control Service write callback + command dispatch.
//
// Command frame layout (22 bytes) :
//   byte 0:     cmd_id (u8)
//   byte 1:     battery_idx (u8)
//   byte 2-5:   param (u32 BE) — reserved
//   byte 6-9:   nonce_lo (u32 BE)
//   byte 10-13: nonce_hi (u32 BE)
//   byte 14-21: hmac (8 bytes, truncated HMAC-SHA256)

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "os/os_mbuf.h"

#include "bmu_core.h"
#include "bmu_ble_control.h"
#include "bmu_ble_hmac.h"
#include "bmu_ble_audit.h"

static const char *TAG = "bmu-ble-ctrl";

/* ---- Extern accessor for BmuCore pointer ---- */
extern struct BmuCore *bmu_ble_gatt_get_core(void);

/* ---- Per-connection state ---- */
#define CTRL_MAX_CONN 4
#define CTRL_FRAME_SIZE 22
#define CMD_ID_REBOOT 0x07

/* Token bucket rate limiter */
#define TOKEN_BUCKET_MAX    10
#define TOKEN_REFILL_US     100000  /* 1 token per 100 ms */

typedef struct {
    uint16_t conn_handle;
    uint64_t last_nonce;
    uint8_t  token_bucket;
    uint64_t last_refill_us;
    bool     active;
} conn_ctrl_state_t;

static conn_ctrl_state_t s_conn_states[CTRL_MAX_CONN];

/* ---- Helpers ---- */

static conn_ctrl_state_t *find_or_alloc_state(uint16_t conn_handle) {
    /* Try to find existing */
    for (int i = 0; i < CTRL_MAX_CONN; i++) {
        if (s_conn_states[i].active && s_conn_states[i].conn_handle == conn_handle) {
            return &s_conn_states[i];
        }
    }
    /* Allocate new slot */
    for (int i = 0; i < CTRL_MAX_CONN; i++) {
        if (!s_conn_states[i].active) {
            s_conn_states[i].conn_handle = conn_handle;
            s_conn_states[i].last_nonce = 0;
            s_conn_states[i].token_bucket = TOKEN_BUCKET_MAX;
            s_conn_states[i].last_refill_us = (uint64_t)esp_timer_get_time();
            s_conn_states[i].active = true;
            return &s_conn_states[i];
        }
    }
    return NULL;
}

static uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static bool rate_limit_check(conn_ctrl_state_t *st) {
    uint64_t now_us = (uint64_t)esp_timer_get_time();
    uint64_t elapsed = now_us - st->last_refill_us;
    uint8_t refill = (uint8_t)(elapsed / TOKEN_REFILL_US);
    if (refill > 0) {
        st->token_bucket += refill;
        if (st->token_bucket > TOKEN_BUCKET_MAX) {
            st->token_bucket = TOKEN_BUCKET_MAX;
        }
        st->last_refill_us = now_us;
    }
    if (st->token_bucket == 0) {
        return false;
    }
    st->token_bucket--;
    return true;
}

/* ---- Delayed reboot timer callback ---- */

static void reboot_timer_cb(TimerHandle_t xTimer) {
    (void)xTimer;
    ESP_LOGW(TAG, "Reboot command: restarting now");
    esp_restart();
}

/* ---- Write callback ---- */

int bmu_ble_control_write_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    (void)attr_handle;
    (void)arg;

    /* 1. Check encrypted + bonded */
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGW(TAG, "conn_find failed: %d", rc);
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (!desc.sec_state.encrypted || !desc.sec_state.bonded) {
        ESP_LOGW(TAG, "write rejected: not encrypted/bonded");
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, 0xFF, "not_encrypted");
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    /* 2. Flatten mbuf and check frame size */
    uint8_t frame[CTRL_FRAME_SIZE];
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len != CTRL_FRAME_SIZE) {
        ESP_LOGW(TAG, "bad frame size: %u", len);
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, 0xFF, "bad_frame_size");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    rc = ble_hs_mbuf_to_flat(ctxt->om, frame, sizeof(frame), &len);
    if (rc != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Parse frame fields */
    uint8_t  cmd_id      = frame[0];
    uint8_t  battery_idx = frame[1];
    uint32_t param       = read_u32_be(&frame[2]);
    uint32_t nonce_lo    = read_u32_be(&frame[6]);
    uint32_t nonce_hi    = read_u32_be(&frame[10]);
    uint64_t nonce       = ((uint64_t)nonce_hi << 32) | (uint64_t)nonce_lo;
    const uint8_t *hmac_tag = &frame[14];

    /* 3. Rate limit */
    conn_ctrl_state_t *st = find_or_alloc_state(conn_handle);
    if (st == NULL) {
        ESP_LOGW(TAG, "no conn state slot available");
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, cmd_id, "no_slot");
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (!rate_limit_check(st)) {
        ESP_LOGW(TAG, "rate limit exceeded for conn %u", conn_handle);
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, cmd_id, "rate_limit");
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* 4. HMAC verify (first 14 bytes signed) */
    if (!bmu_ble_hmac_verify(conn_handle, frame, 14, hmac_tag, 8)) {
        ESP_LOGW(TAG, "HMAC verify failed for conn %u", conn_handle);
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, cmd_id, "hmac_fail");
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    /* 5. Anti-replay: nonce must be strictly greater than last seen */
    if (nonce <= st->last_nonce) {
        ESP_LOGW(TAG, "anti-replay: nonce %" PRIu64 " <= %" PRIu64,
                 nonce, st->last_nonce);
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, cmd_id, "replay");
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* 6. Persist nonce BEFORE dispatching (critical for reboot safety) */
    st->last_nonce = nonce;
    bmu_ble_hmac_persist_nonce(&desc.peer_ota_addr, nonce);

    /* 7. Dispatch command */
    if (cmd_id == CMD_ID_REBOOT) {
        /* Reboot via FreeRTOS timer — NOT a bmu_core_command */
        ESP_LOGW(TAG, "Reboot command received, scheduling restart in 500 ms");
        bmu_ble_audit_log_pass(&desc.peer_ota_addr, cmd_id, 0, param, 0);

        TimerHandle_t t = xTimerCreate("reboot", pdMS_TO_TICKS(500),
                                       pdFALSE, NULL, reboot_timer_cb);
        if (t != NULL) {
            xTimerStart(t, 0);
        }
        return 0;
    }

    /* Map cmd_id to BmuCommandC.kind (1:1 for 0x01..0x04) */
    if (cmd_id < 1 || cmd_id > 4) {
        ESP_LOGW(TAG, "unsupported cmd_id: 0x%02x", cmd_id);
        bmu_ble_audit_log_reject(&desc.peer_ota_addr, cmd_id, "unsupported");
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    struct BmuCore *core = bmu_ble_gatt_get_core();
    if (core == NULL) {
        ESP_LOGE(TAG, "core not available");
        return BLE_ATT_ERR_UNLIKELY;
    }

    struct BmuCommandC cmd = {0};
    cmd.kind = cmd_id;
    cmd.target_idx = battery_idx;

    int32_t result = bmu_core_command(core, &cmd);
    ESP_LOGI(TAG, "cmd 0x%02x bat=%u -> result=%ld", cmd_id, battery_idx, (long)result);

    /* 8. Audit log */
    bmu_ble_audit_log_pass(&desc.peer_ota_addr, cmd_id, battery_idx, param, (int)result);

    return (result == BMU_OK) ? 0 : BLE_ATT_ERR_UNLIKELY;
}

/* ---- Disconnect cleanup ---- */

void bmu_ble_control_on_disconnect(uint16_t conn_handle) {
    for (int i = 0; i < CTRL_MAX_CONN; i++) {
        if (s_conn_states[i].active && s_conn_states[i].conn_handle == conn_handle) {
            s_conn_states[i].active = false;
            return;
        }
    }
}
