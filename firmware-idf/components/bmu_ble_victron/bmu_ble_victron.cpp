/**
 * @file bmu_ble_victron.cpp
 * @brief Victron BLE Instant Readout — advertising encrypted battery/solar data.
 *
 * Victron Connect reads Manufacturer Specific Data (Company ID 0x02E1) from
 * BLE advertising packets. Data is AES-CTR-128 encrypted.
 *
 * References:
 * - https://github.com/keshavdv/victron-ble
 * - Victron Company ID: 0x02E1
 */
#include "sdkconfig.h"

#if CONFIG_BMU_VICTRON_BLE_ENABLED

#include "bmu_ble_victron.h"
#include "bmu_vedirect.h"
#include "bmu_config.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "mbedtls/aes.h"

#include <cstring>
#include <cstdio>

static const char *TAG = "BLE_VIC";

#define VICTRON_COMPANY_ID      0x02E1
#define VICTRON_RECORD_SOLAR    0x01
#define VICTRON_RECORD_BATTERY  0x02

static bmu_protection_ctx_t *s_prot = NULL;
static bmu_battery_manager_t *s_mgr = NULL;
static uint8_t s_nb_ina = 0;
static uint16_t s_adv_counter = 0;
static uint8_t s_aes_key[16] = {};
static esp_timer_handle_t s_adv_timer = NULL;
static int s_adv_slot = 0;

/* ── AES key parse from hex string ── */

static void parse_hex_key(const char *hex, uint8_t *out)
{
    for (int i = 0; i < 16; i++) {
        unsigned byte = 0;
        sscanf(hex + i * 2, "%02x", &byte);
        out[i] = (uint8_t)byte;
    }
}

/* ── AES-CTR encrypt ── */

static void encrypt_payload(const uint8_t *plain, uint8_t *cipher,
                            size_t len, uint16_t counter)
{
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, s_aes_key, 128);

    uint8_t nonce[16] = {};
    nonce[0] = (uint8_t)(counter & 0xFF);
    nonce[1] = (uint8_t)(counter >> 8);

    size_t nc_off = 0;
    uint8_t stream[16] = {};
    mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce, stream, plain, cipher);
    mbedtls_aes_free(&aes);
}

/* ── Build Battery Monitor advertising data ── */

static int build_battery_adv(uint8_t *buf, size_t buf_len)
{
    if (s_nb_ina == 0) return 0;

    float avg_mv = 0.0f;
    float total_i = 0.0f;
    if (bmu_battery_manager_get_summary(s_mgr, &avg_mv, &total_i, NULL) != ESP_OK) {
        return 0;
    }
    float avg_v = avg_mv / 1000.0f;

    /* SOC estimation */
    float v_min = 24.0f;  /* CONFIG_BMU_VRM_SOC_V_MIN / 1000 */
    float v_max = 28.8f;  /* CONFIG_BMU_VRM_SOC_V_MAX / 1000 */
    float soc = (avg_v - v_min) / (v_max - v_min) * 100.0f;
    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;

    /* Sum Ah */
    float sum_ah_c = 0, sum_ah_d = 0;
    int nb = s_nb_ina > 16 ? 16 : s_nb_ina;
    for (int i = 0; i < nb; i++) {
        sum_ah_c += bmu_battery_manager_get_ah_charge(s_mgr, i);
        sum_ah_d += bmu_battery_manager_get_ah_discharge(s_mgr, i);
    }

    /* Pack plaintext (10 bytes LE):
     * remaining_ah(u16 0.1Ah), voltage(u16 0.01V), current(i16 0.1A),
     * soc(u16 0.1%), consumed_ah(u16 0.1Ah) */
    uint8_t plain[10];
    uint16_t remaining_ah = (uint16_t)(sum_ah_c * 10.0f);
    uint16_t voltage = (uint16_t)(avg_v * 100.0f);
    int16_t current = (int16_t)(total_i * 10.0f);
    uint16_t soc_u16 = (uint16_t)(soc * 10.0f);
    uint16_t consumed_ah = (uint16_t)(sum_ah_d * 10.0f);

    memcpy(plain + 0, &remaining_ah, 2);
    memcpy(plain + 2, &voltage, 2);
    memcpy(plain + 4, &current, 2);
    memcpy(plain + 6, &soc_u16, 2);
    memcpy(plain + 8, &consumed_ah, 2);

    uint8_t cipher[10];
    encrypt_payload(plain, cipher, 10, s_adv_counter);

    /* Manufacturer specific data:
     * [company_lo, company_hi, record_type, counter_lo, counter_hi, encrypted...] */
    if (buf_len < 15) return 0;
    buf[0] = (uint8_t)(VICTRON_COMPANY_ID & 0xFF);
    buf[1] = (uint8_t)(VICTRON_COMPANY_ID >> 8);
    buf[2] = VICTRON_RECORD_BATTERY;
    buf[3] = (uint8_t)(s_adv_counter & 0xFF);
    buf[4] = (uint8_t)(s_adv_counter >> 8);
    memcpy(buf + 5, cipher, 10);
    return 15;
}

/* ── Build Solar Charger advertising data ── */

static int build_solar_adv(uint8_t *buf, size_t buf_len)
{
    if (!bmu_vedirect_is_connected()) return 0;
    const bmu_vedirect_data_t *d = bmu_vedirect_get_data();
    if (!d || !d->valid) return 0;

    /* Pack plaintext (10 bytes LE):
     * state(u8), error(u8), yield_today(u16 0.01kWh),
     * pv_power(u16 W), bat_current(i16 0.1A), bat_voltage(u16 0.01V) */
    uint8_t plain[10];
    plain[0] = d->charge_state;
    plain[1] = d->error_code;
    uint16_t yield = (uint16_t)(d->yield_today_wh / 10);
    uint16_t ppv = d->panel_power_w;
    int16_t ibat = (int16_t)(d->battery_current_a * 10.0f);
    uint16_t vbat = (uint16_t)(d->battery_voltage_v * 100.0f);

    memcpy(plain + 2, &yield, 2);
    memcpy(plain + 4, &ppv, 2);
    memcpy(plain + 6, &ibat, 2);
    memcpy(plain + 8, &vbat, 2);

    uint8_t cipher[10];
    encrypt_payload(plain, cipher, 10, s_adv_counter);

    if (buf_len < 15) return 0;
    buf[0] = (uint8_t)(VICTRON_COMPANY_ID & 0xFF);
    buf[1] = (uint8_t)(VICTRON_COMPANY_ID >> 8);
    buf[2] = VICTRON_RECORD_SOLAR;
    buf[3] = (uint8_t)(s_adv_counter & 0xFF);
    buf[4] = (uint8_t)(s_adv_counter >> 8);
    memcpy(buf + 5, cipher, 10);
    return 15;
}

/* ── Advertising rotation timer callback ── */

static void adv_rotate_cb(void *arg)
{
    (void)arg;
    s_adv_slot = (s_adv_slot + 1) % 3;

    /* Slot 0: KXKM-BMU (let existing BLE advertising handle it) */
    if (s_adv_slot == 0) return;

    uint8_t mfr_data[16] = {};
    int mfr_len = 0;

    if (s_adv_slot == 1) {
        mfr_len = build_battery_adv(mfr_data, sizeof(mfr_data));
    } else {
        mfr_len = build_solar_adv(mfr_data, sizeof(mfr_data));
    }

    if (mfr_len == 0) return;
    s_adv_counter++;

    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.mfg_data = mfr_data;
    fields.mfg_data_len = (uint8_t)mfr_len;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGD(TAG, "adv_set_fields rc=%d", rc);
    }
}

/* ── Public API ── */

esp_err_t bmu_ble_victron_init(bmu_protection_ctx_t *prot,
                               bmu_battery_manager_t *mgr,
                               uint8_t nb_ina)
{
    s_prot = prot;
    s_mgr = mgr;
    s_nb_ina = nb_ina;

    const char *hex_key = CONFIG_BMU_VICTRON_BLE_KEY;
    if (strlen(hex_key) != 32) {
        ESP_LOGE(TAG, "Invalid BLE key: expected 32 hex chars, got %d",
                 (int)strlen(hex_key));
        return ESP_ERR_INVALID_ARG;
    }
    parse_hex_key(hex_key, s_aes_key);

    const esp_timer_create_args_t timer_args = {
        .callback = adv_rotate_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "vic_adv",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_adv_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(
        s_adv_timer, CONFIG_BMU_VICTRON_ADV_INTERVAL_MS * 1000ULL));

    ESP_LOGI(TAG, "Victron BLE Instant Readout — %dms rotation",
             CONFIG_BMU_VICTRON_ADV_INTERVAL_MS);
    return ESP_OK;
}

#else

#include "bmu_ble_victron.h"
esp_err_t bmu_ble_victron_init(bmu_protection_ctx_t *p,
                               bmu_battery_manager_t *m, uint8_t n)
{ (void)p; (void)m; (void)n; return ESP_OK; }

#endif
