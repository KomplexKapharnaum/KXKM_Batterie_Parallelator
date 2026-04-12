// firmware-idf-v2/components/bmu_ble/src/bmu_ble_audit.c
//
// Phase 19 : Append-only audit log with HMAC chain.
//
// Audit log at /fatfs/audit.log, one line per entry.
// Each line ends with a 4-byte (8 hex chars) HMAC chain tag:
//   HMAC-SHA256(line_content || prev_hmac)[0..4]
//
// Audit key: random 32 bytes stored in NVS "bmu" / "audit_key".
// NOTE: audit key NOT encrypted in dev builds — document this limitation.

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_timer.h"

#include "mbedtls/md.h"

#include "bmu_ble_audit.h"

static const char *TAG = "bmu-ble-audit";

#define AUDIT_LOG_PATH  "/fatfs/audit.log"
#define AUDIT_KEY_LEN   32
#define CHAIN_TAG_LEN   4
#define NVS_NAMESPACE   "bmu"
#define NVS_KEY_AKEY    "audit_key"

static uint8_t s_audit_key[AUDIT_KEY_LEN];
static uint8_t s_prev_chain[CHAIN_TAG_LEN];
static bool    s_inited = false;

/* ---- Load or generate audit key from NVS ---- */

static void load_or_gen_audit_key(void) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        /* Generate ephemeral key — won't persist across reboots */
        esp_fill_random(s_audit_key, AUDIT_KEY_LEN);
        return;
    }

    size_t len = AUDIT_KEY_LEN;
    err = nvs_get_blob(h, NVS_KEY_AKEY, s_audit_key, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND || len != AUDIT_KEY_LEN) {
        /* First boot — generate and persist */
        esp_fill_random(s_audit_key, AUDIT_KEY_LEN);
        err = nvs_set_blob(h, NVS_KEY_AKEY, s_audit_key, AUDIT_KEY_LEN);
        if (err == ESP_OK) {
            nvs_commit(h);
            ESP_LOGI(TAG, "audit key generated and persisted");
        } else {
            ESP_LOGW(TAG, "failed to persist audit key: %s", esp_err_to_name(err));
        }
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "audit key loaded from NVS");
    } else {
        ESP_LOGW(TAG, "nvs_get_blob failed: %s, using random key", esp_err_to_name(err));
        esp_fill_random(s_audit_key, AUDIT_KEY_LEN);
    }
    nvs_close(h);
}

/* ---- Compute HMAC chain tag ---- */

static void compute_chain_tag(const char *line, size_t line_len,
                               const uint8_t *prev, uint8_t *out_tag) {
    /* HMAC-SHA256(key, line_content || prev_chain)[0..CHAIN_TAG_LEN] */
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, md_info, 1 /* HMAC */);
    mbedtls_md_hmac_starts(&ctx, s_audit_key, AUDIT_KEY_LEN);
    mbedtls_md_hmac_update(&ctx, (const uint8_t *)line, line_len);
    mbedtls_md_hmac_update(&ctx, prev, CHAIN_TAG_LEN);

    uint8_t full_hmac[32];
    mbedtls_md_hmac_finish(&ctx, full_hmac);
    mbedtls_md_free(&ctx);

    memcpy(out_tag, full_hmac, CHAIN_TAG_LEN);
    memset(full_hmac, 0, sizeof(full_hmac));
}

/* ---- Format peer address ---- */

static void fmt_peer(const ble_addr_t *peer, char *out, size_t out_sz) {
    snprintf(out, out_sz, "%02X%02X%02X%02X%02X%02X",
             peer->val[5], peer->val[4], peer->val[3],
             peer->val[2], peer->val[1], peer->val[0]);
}

/* ---- Write a line to audit log ---- */

static void write_audit_line(const char *line) {
    if (!s_inited) {
        return;
    }

    size_t line_len = strlen(line);

    /* Compute chain tag */
    uint8_t chain_tag[CHAIN_TAG_LEN];
    compute_chain_tag(line, line_len, s_prev_chain, chain_tag);

    /* Write to file: line + "|" + hex(chain_tag) + "\n" */
    FILE *f = fopen(AUDIT_LOG_PATH, "ab");
    if (f == NULL) {
        ESP_LOGW(TAG, "failed to open %s", AUDIT_LOG_PATH);
        return;
    }

    fprintf(f, "%s|", line);
    for (int i = 0; i < CHAIN_TAG_LEN; i++) {
        fprintf(f, "%02x", chain_tag[i]);
    }
    fprintf(f, "\n");
    fflush(f);
    fclose(f);

    /* Update chain state */
    memcpy(s_prev_chain, chain_tag, CHAIN_TAG_LEN);
}

/* ---- Public API ---- */

void bmu_ble_audit_init(void) {
    load_or_gen_audit_key();
    memset(s_prev_chain, 0, CHAIN_TAG_LEN);
    s_inited = true;
    ESP_LOGI(TAG, "audit log initialized at %s", AUDIT_LOG_PATH);
}

void bmu_ble_audit_log_pass(const ble_addr_t *peer, uint8_t cmd_id,
                              uint8_t bat_idx, uint32_t param, int result) {
    char peer_str[16];
    fmt_peer(peer, peer_str, sizeof(peer_str));

    char line[128];
    int64_t ts = esp_timer_get_time();
    snprintf(line, sizeof(line), "PASS,%lld,%s,0x%02X,%u,%lu,%d",
             (long long)ts, peer_str, cmd_id, bat_idx,
             (unsigned long)param, result);

    write_audit_line(line);
}

void bmu_ble_audit_log_reject(const ble_addr_t *peer, uint8_t cmd_id,
                                const char *reason) {
    char peer_str[16];
    if (peer != NULL) {
        fmt_peer(peer, peer_str, sizeof(peer_str));
    } else {
        snprintf(peer_str, sizeof(peer_str), "UNKNOWN");
    }

    char line[128];
    int64_t ts = esp_timer_get_time();
    snprintf(line, sizeof(line), "REJECT,%lld,%s,0x%02X,%s",
             (long long)ts, peer_str, cmd_id, reason ? reason : "?");

    write_audit_line(line);
}
