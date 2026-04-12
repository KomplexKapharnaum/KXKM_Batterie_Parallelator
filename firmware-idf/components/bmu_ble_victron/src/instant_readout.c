// firmware-idf-v2/components/bmu_ble_victron/src/instant_readout.c
//
// Phase 21 : Victron Instant Readout — manufacturer data in scan response.
// AES-CTR encrypted payload with auto-generated bind key stored in NVS.

#include "bmu_ble_victron.h"
#include "fleet_agg.h"
#include "bmu_core.h"

#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/aes.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"

static const char *TAG = "bmu-victron-ir";

/* ---- Bind key (16 bytes AES-128) ---- */
static uint8_t s_bind_key[16];
static bool    s_key_loaded = false;

/* ---- NVS key name (max 15 chars) ---- */
#define NVS_NAMESPACE "bmu"
#define NVS_KEY       "victron_bk"

/* ---- Victron manufacturer data header ---- */
#define VICTRON_COMPANY_LO  0xE1
#define VICTRON_COMPANY_HI  0x02
#define VICTRON_PRODUCT_SS  0xA3  /* SmartShunt product type */

/* ---- Payload sizes ---- */
#define PLAINTEXT_LEN  13
#define MFG_HEADER_LEN  3
#define MFG_TOTAL_LEN  (MFG_HEADER_LEN + PLAINTEXT_LEN)  /* 16 bytes */

/* ---- AES-CTR nonce counter (incrementing) ---- */
static uint32_t s_nonce_counter = 0;

/* ---- Load or generate bind key from NVS ---- */
static void ensure_bind_key(void) {
    if (s_key_loaded) return;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        /* Generate ephemeral key */
        esp_fill_random(s_bind_key, sizeof(s_bind_key));
        s_key_loaded = true;
        return;
    }

    size_t len = sizeof(s_bind_key);
    err = nvs_get_blob(nvs, NVS_KEY, s_bind_key, &len);
    if (err == ESP_OK && len == sizeof(s_bind_key)) {
        ESP_LOGI(TAG, "Bind key loaded from NVS");
    } else {
        /* Generate and persist new key */
        esp_fill_random(s_bind_key, sizeof(s_bind_key));
        err = nvs_set_blob(nvs, NVS_KEY, s_bind_key, sizeof(s_bind_key));
        if (err == ESP_OK) {
            nvs_commit(nvs);
            ESP_LOGI(TAG, "New bind key generated and saved to NVS");
        } else {
            ESP_LOGW(TAG, "Failed to save bind key: %s", esp_err_to_name(err));
        }
    }

    nvs_close(nvs);
    s_key_loaded = true;
}

/* ---- Encrypt plaintext with AES-128-CTR ---- */
static void aes_ctr_encrypt(const uint8_t *plaintext, uint8_t *ciphertext,
                            size_t len) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, s_bind_key, 128);

    /* Nonce: 12 bytes zero + 4 bytes counter (LE) */
    uint8_t nonce[16];
    memset(nonce, 0, sizeof(nonce));
    nonce[12] = (uint8_t)(s_nonce_counter & 0xFF);
    nonce[13] = (uint8_t)((s_nonce_counter >> 8) & 0xFF);
    nonce[14] = (uint8_t)((s_nonce_counter >> 16) & 0xFF);
    nonce[15] = (uint8_t)((s_nonce_counter >> 24) & 0xFF);
    s_nonce_counter++;

    size_t nc_off = 0;
    uint8_t stream_block[16];
    mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce, stream_block,
                          plaintext, ciphertext);

    mbedtls_aes_free(&aes);
}

/* ---- Public API ---- */

void bmu_ble_victron_adv_tick(struct BmuCore *core) {
    if (!core) return;

    ensure_bind_key();

    /* Get fleet aggregation */
    struct BmuSnapshotC snap;
    int32_t rc = bmu_core_get_cached_snapshot(core, &snap);
    if (rc != 0) return;

    bmu_fleet_agg_t agg;
    fleet_agg_compute(&snap, &agg);

    /* Build plaintext payload (13 bytes) */
    uint8_t plain[PLAINTEXT_LEN];
    memset(plain, 0, sizeof(plain));

    /* voltage_mv LE u16 */
    plain[0] = (uint8_t)(agg.voltage_mv & 0xFF);
    plain[1] = (uint8_t)((agg.voltage_mv >> 8) & 0xFF);

    /* current_cA LE i16 (convert mA to centi-amps: divide by 10) */
    int16_t current_ca = (int16_t)(agg.current_ma / 10);
    plain[2] = (uint8_t)(current_ca & 0xFF);
    plain[3] = (uint8_t)((current_ca >> 8) & 0xFF);

    /* soc_pct u8 */
    plain[4] = agg.soc_pct;

    /* consumed_ah LE u32 (mAh) */
    uint32_t ah = (uint32_t)agg.consumed_ah_mah;
    plain[5] = (uint8_t)(ah & 0xFF);
    plain[6] = (uint8_t)((ah >> 8) & 0xFF);
    plain[7] = (uint8_t)((ah >> 16) & 0xFF);
    plain[8] = (uint8_t)((ah >> 24) & 0xFF);

    /* temp_max i8 */
    plain[9] = (uint8_t)agg.temp_max_c;

    /* n_online u8 */
    plain[10] = agg.n_online;

    /* pad 2 bytes (already zero from memset) */

    /* Encrypt */
    uint8_t cipher[PLAINTEXT_LEN];
    aes_ctr_encrypt(plain, cipher, PLAINTEXT_LEN);

    /* Build manufacturer data: header + encrypted payload */
    uint8_t mfg_data[MFG_TOTAL_LEN];
    mfg_data[0] = VICTRON_COMPANY_LO;
    mfg_data[1] = VICTRON_COMPANY_HI;
    mfg_data[2] = VICTRON_PRODUCT_SS;
    memcpy(&mfg_data[MFG_HEADER_LEN], cipher, PLAINTEXT_LEN);

    /* Update scan response with Victron manufacturer data */
    struct ble_hs_adv_fields rsp_fields = {0};
    rsp_fields.mfg_data = mfg_data;
    rsp_fields.mfg_data_len = MFG_TOTAL_LEN;

    int ret = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (ret != 0 && ret != BLE_HS_EBUSY) {
        ESP_LOGW(TAG, "ble_gap_adv_rsp_set_fields failed: %d", ret);
    }
}
