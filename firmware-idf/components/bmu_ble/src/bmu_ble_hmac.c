// firmware-idf-v2/components/bmu_ble/src/bmu_ble_hmac.c
//
// Phase 19 : HKDF key derivation from BLE LTK + HMAC verify + nonce NVS.
//
// Security notes:
// - NEVER log LTK, derived keys, or HMAC tags
// - Constant-time comparison (xor accumulate)
// - NVS nonce keys limited to 15 chars (NVS key max)

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_store.h"

#include "mbedtls/md.h"

#include "bmu_ble_hmac.h"

static const char *TAG = "bmu-ble-hmac";

#define HMAC_MAX_CONN   4
#define DERIVED_KEY_LEN 32
#define NVS_NAMESPACE   "bmu_nonces"

static const char HKDF_INFO[] = "KXKM-BMU-CMD-v1";

/* Manual HKDF-SHA256 (extract + expand, single OKM block = 32 bytes).
 * ESP-IDF mbedtls doesn't always ship mbedtls_hkdf(), so we do it by hand. */
static int hkdf_sha256(const uint8_t *salt, size_t salt_len,
                        const uint8_t *ikm, size_t ikm_len,
                        const uint8_t *info, size_t info_len,
                        uint8_t *okm, size_t okm_len) {
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t prk[32]; /* extract output */
    int ret;

    /* Extract: PRK = HMAC-SHA256(salt, IKM) */
    ret = mbedtls_md_hmac(md, salt, salt_len, ikm, ikm_len, prk);
    if (ret != 0) return ret;

    /* Expand: single block (okm_len <= 32).
     * T(1) = HMAC-SHA256(PRK, info || 0x01) */
    if (okm_len > 32) okm_len = 32;

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    ret = mbedtls_md_setup(&ctx, md, 1);
    if (ret != 0) { mbedtls_md_free(&ctx); return ret; }

    mbedtls_md_hmac_starts(&ctx, prk, 32);
    mbedtls_md_hmac_update(&ctx, info, info_len);
    uint8_t one = 0x01;
    mbedtls_md_hmac_update(&ctx, &one, 1);

    uint8_t t[32];
    mbedtls_md_hmac_finish(&ctx, t);
    mbedtls_md_free(&ctx);

    memcpy(okm, t, okm_len);
    memset(prk, 0, sizeof(prk));
    memset(t, 0, sizeof(t));
    return 0;
}

/* Per-connection derived key store */
typedef struct {
    uint16_t conn_handle;
    uint8_t  key[DERIVED_KEY_LEN];
    bool     active;
} conn_key_slot_t;

static conn_key_slot_t s_key_slots[HMAC_MAX_CONN];

/* ---- Constant-time comparison ---- */

static bool ct_compare(const uint8_t *a, const uint8_t *b, size_t len) {
    volatile uint8_t acc = 0;
    for (size_t i = 0; i < len; i++) {
        acc |= a[i] ^ b[i];
    }
    return acc == 0;
}

/* ---- NVS nonce key from peer address ---- */
/* NVS key max = 15 chars. Use "N" + 12 hex chars of MAC = 13 chars */

static void make_nvs_key(const ble_addr_t *peer, char *out, size_t out_sz) {
    snprintf(out, out_sz, "N%02X%02X%02X%02X%02X%02X",
             peer->val[5], peer->val[4], peer->val[3],
             peer->val[2], peer->val[1], peer->val[0]);
}

/* ---- HKDF derive from LTK ---- */

void bmu_ble_hmac_derive_for_conn(uint16_t conn_handle) {
    struct ble_gap_conn_desc desc;
    int rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGW(TAG, "derive: conn_find failed: %d", rc);
        return;
    }

    /* Read peer security record to get LTK */
    struct ble_store_key_sec key_sec = {0};
    struct ble_store_value_sec value_sec = {0};

    key_sec.peer_addr = desc.peer_ota_addr;
    key_sec.idx = 0;

    rc = ble_store_read_peer_sec(&key_sec, &value_sec);
    if (rc != 0) {
        ESP_LOGW(TAG, "derive: store_read_peer_sec failed: %d", rc);
        return;
    }

    if (!value_sec.ltk_present) {
        ESP_LOGW(TAG, "derive: no LTK present for peer");
        return;
    }

    /* HKDF-SHA256(IKM=LTK, salt=peer_addr, info="KXKM-BMU-CMD-v1") */
    uint8_t derived[DERIVED_KEY_LEN];

    rc = hkdf_sha256(desc.peer_ota_addr.val, 6,           /* salt = peer addr */
                     value_sec.ltk, value_sec.key_size,    /* IKM = LTK */
                     (const uint8_t *)HKDF_INFO, sizeof(HKDF_INFO) - 1,
                     derived, DERIVED_KEY_LEN);

    /* Wipe LTK from stack immediately */
    memset(&value_sec, 0, sizeof(value_sec));

    if (rc != 0) {
        ESP_LOGE(TAG, "derive: HKDF failed: -0x%04x", (unsigned)-rc);
        return;
    }

    /* Store derived key in slot */
    for (int i = 0; i < HMAC_MAX_CONN; i++) {
        if (!s_key_slots[i].active) {
            s_key_slots[i].conn_handle = conn_handle;
            memcpy(s_key_slots[i].key, derived, DERIVED_KEY_LEN);
            s_key_slots[i].active = true;
            memset(derived, 0, sizeof(derived));
            ESP_LOGI(TAG, "derived key stored for conn %u", conn_handle);
            return;
        }
    }

    memset(derived, 0, sizeof(derived));
    ESP_LOGW(TAG, "derive: no free key slot");
}

/* ---- HMAC verify ---- */

bool bmu_ble_hmac_verify(uint16_t conn_handle, const void *msg, size_t msg_len,
                          const uint8_t *tag, size_t tag_len) {
    /* Find key slot */
    conn_key_slot_t *slot = NULL;
    for (int i = 0; i < HMAC_MAX_CONN; i++) {
        if (s_key_slots[i].active && s_key_slots[i].conn_handle == conn_handle) {
            slot = &s_key_slots[i];
            break;
        }
    }
    if (slot == NULL) {
        ESP_LOGW(TAG, "verify: no key for conn %u", conn_handle);
        return false;
    }

    /* Compute HMAC-SHA256 */
    uint8_t computed[32];
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    int rc = mbedtls_md_hmac(md_info, slot->key, DERIVED_KEY_LEN,
                             (const uint8_t *)msg, msg_len,
                             computed);
    if (rc != 0) {
        ESP_LOGE(TAG, "verify: HMAC compute failed: -0x%04x", (unsigned)-rc);
        return false;
    }

    /* Constant-time compare truncated tag (first tag_len bytes) */
    if (tag_len > 32) {
        tag_len = 32;
    }

    bool ok = ct_compare(computed, tag, tag_len);
    memset(computed, 0, sizeof(computed));
    return ok;
}

/* ---- Nonce NVS persistence ---- */

void bmu_ble_hmac_persist_nonce(const ble_addr_t *peer, uint64_t nonce) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(%s) failed: %s", NVS_NAMESPACE, esp_err_to_name(err));
        return;
    }

    char key[16];
    make_nvs_key(peer, key, sizeof(key));

    err = nvs_set_u64(h, key, nonce);
    if (err == ESP_OK) {
        nvs_commit(h);
    } else {
        ESP_LOGW(TAG, "nvs_set_u64 failed: %s", esp_err_to_name(err));
    }
    nvs_close(h);
}

uint64_t bmu_ble_hmac_load_nonce(const ble_addr_t *peer) {
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return 0;
    }

    char key[16];
    make_nvs_key(peer, key, sizeof(key));

    uint64_t nonce = 0;
    err = nvs_get_u64(h, key, &nonce);
    nvs_close(h);

    if (err != ESP_OK) {
        return 0;
    }
    return nonce;
}

/* ---- Invalidate on disconnect ---- */

void bmu_ble_hmac_invalidate_conn(uint16_t conn_handle) {
    for (int i = 0; i < HMAC_MAX_CONN; i++) {
        if (s_key_slots[i].active && s_key_slots[i].conn_handle == conn_handle) {
            memset(s_key_slots[i].key, 0, DERIVED_KEY_LEN);
            s_key_slots[i].active = false;
            ESP_LOGI(TAG, "key invalidated for conn %u", conn_handle);
            return;
        }
    }
}
