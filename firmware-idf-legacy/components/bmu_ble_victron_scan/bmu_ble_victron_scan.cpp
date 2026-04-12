#include "bmu_ble_victron_scan.h"
#include "bmu_config.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "mbedtls/aes.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>

#if !CONFIG_BMU_VIC_SCAN_ENABLED
/* Stubs disabled */
esp_err_t bmu_vic_scan_init(void) { return ESP_OK; }
esp_err_t bmu_vic_scan_start(void) { return ESP_OK; }
void bmu_vic_scan_stop(void) {}
int bmu_vic_scan_get_devices(bmu_vic_device_t *, int) { return 0; }
const bmu_vic_device_t *bmu_vic_scan_get_device_by_mac(const uint8_t *) { return NULL; }
int bmu_vic_scan_count(void) { return 0; }
#else

static const char *TAG = "VIC_SCAN";
#define VICTRON_COMPANY_ID 0x02E1
#define EXPIRY_MS (5 * 60 * 1000) /* 5 minutes */

static bmu_vic_device_t s_devices[CONFIG_BMU_VIC_SCAN_MAX_DEVICES];
static int s_device_count = 0;
static esp_timer_handle_t s_scan_timer = NULL;
static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

/* ── AES decrypt (same algo as bmu_ble_victron) ──────────────────── */
static bool decrypt_payload(const uint8_t mac[6], const uint8_t *cipher,
                            uint8_t *plain, size_t len, uint16_t counter)
{
    char hex_key[33];
    if (bmu_config_get_victron_device_key(mac, hex_key, sizeof(hex_key)) != ESP_OK)
        return false;

    uint8_t key[16];
    for (int i = 0; i < 16; i++) {
        unsigned b = 0;
        sscanf(hex_key + i * 2, "%02x", &b);
        key[i] = (uint8_t)b;
    }

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);
    uint8_t nonce[16] = {};
    nonce[0] = (uint8_t)(counter & 0xFF);
    nonce[1] = (uint8_t)(counter >> 8);
    size_t nc_off = 0;
    uint8_t stream[16] = {};
    mbedtls_aes_crypt_ctr(&aes, len, &nc_off, nonce, stream, cipher, plain);
    mbedtls_aes_free(&aes);
    return true;
}

/* ── Parse decoded payload by record type ─────────────────────────── */
static void parse_payload(bmu_vic_device_t *dev)
{
    const uint8_t *p = dev->raw_decrypted;
    switch (dev->record_type) {
    case 0x01: /* Solar */
        dev->solar.cs       = p[0];
        dev->solar.err      = p[1];
        memcpy(&dev->solar.yield_wh, p + 2, 2);
        memcpy(&dev->solar.ppv_w,    p + 4, 2);
        memcpy(&dev->solar.ibat_da,  p + 6, 2);
        memcpy(&dev->solar.vbat_cv,  p + 8, 2);
        break;
    case 0x02: /* Battery */
        memcpy(&dev->battery.rem_ah_dah, p + 0, 2);
        memcpy(&dev->battery.v_cv,       p + 2, 2);
        memcpy(&dev->battery.i_da,       p + 4, 2);
        memcpy(&dev->battery.soc_pm,     p + 6, 2);
        memcpy(&dev->battery.cons_dah,   p + 8, 2);
        break;
    case 0x03: /* Inverter */
        memcpy(&dev->inverter.vac_cv,  p + 0, 2);
        memcpy(&dev->inverter.iac_da,  p + 2, 2);
        dev->inverter.state = p[4];
        break;
    case 0x04: /* DC-DC */
        memcpy(&dev->dcdc.vin_cv,  p + 0, 2);
        memcpy(&dev->dcdc.vout_cv, p + 2, 2);
        dev->dcdc.state = p[4];
        break;
    }
}

/* ── Find or allocate device slot ─────────────────────────────────── */
static bmu_vic_device_t *find_or_alloc(const uint8_t mac[6])
{
    for (int i = 0; i < s_device_count; i++) {
        if (memcmp(s_devices[i].mac, mac, 6) == 0) return &s_devices[i];
    }
    if (s_device_count < CONFIG_BMU_VIC_SCAN_MAX_DEVICES) {
        bmu_vic_device_t *d = &s_devices[s_device_count++];
        memset(d, 0, sizeof(*d));
        memcpy(d->mac, mac, 6);
        /* Load label from NVS */
        bmu_config_get_victron_device_label(mac, d->label, sizeof(d->label));
        return d;
    }
    /* Evict oldest */
    int oldest = 0;
    for (int i = 1; i < s_device_count; i++) {
        if (s_devices[i].last_seen_ms < s_devices[oldest].last_seen_ms)
            oldest = i;
    }
    bmu_vic_device_t *d = &s_devices[oldest];
    memset(d, 0, sizeof(*d));
    memcpy(d->mac, mac, 6);
    bmu_config_get_victron_device_label(mac, d->label, sizeof(d->label));
    return d;
}

/* ── GAP event handler for scan results ───────────────────────────── */
static int scan_event_handler(struct ble_gap_event *event, void *arg)
{
    if (event->type != BLE_GAP_EVENT_DISC) return 0;

    struct ble_hs_adv_fields parsed;
    if (ble_hs_adv_parse_fields(&parsed, event->disc.data,
                                 event->disc.length_data) != 0)
        return 0;

    /* Filter: must have manufacturer data with Victron company ID */
    if (parsed.mfg_data == NULL || parsed.mfg_data_len < 15) return 0;
    uint16_t company = parsed.mfg_data[0] | (parsed.mfg_data[1] << 8);
    if (company != VICTRON_COMPANY_ID) return 0;

    /* Detect format: 0x10 at byte 2 = new Instant Readout with PID (18 bytes) */
    uint8_t record_type;
    uint16_t counter;
    const uint8_t *cipher;
    if (parsed.mfg_data[2] == 0x10 && parsed.mfg_data_len >= 18) {
        /* New format: [company(2)] [0x10] [pid(2)] [record] [nonce(2)] [encrypted(10)] */
        record_type = parsed.mfg_data[5];
        counter = parsed.mfg_data[6] | (parsed.mfg_data[7] << 8);
        cipher = parsed.mfg_data + 8;
    } else {
        /* Legacy format: [company(2)] [record] [nonce(2)] [encrypted(10)] */
        record_type = parsed.mfg_data[2];
        counter = parsed.mfg_data[3] | (parsed.mfg_data[4] << 8);
        cipher = parsed.mfg_data + 5;
    }

    uint8_t mac[6];
    memcpy(mac, event->disc.addr.val, 6);

    bmu_vic_device_t *dev = find_or_alloc(mac);
    dev->record_type = record_type;
    dev->last_seen_ms = now_ms();

    /* Try decryption */
    char hex_key[33];
    dev->key_configured = (bmu_config_get_victron_device_key(mac, hex_key, sizeof(hex_key)) == ESP_OK);
    if (dev->key_configured) {
        dev->decrypted = decrypt_payload(mac, cipher, dev->raw_decrypted, 10, counter);
        if (dev->decrypted) parse_payload(dev);
    } else {
        dev->decrypted = false;
    }

    return 0;
}

/* ── Periodic scan trigger ────────────────────────────────────────── */
static void scan_timer_cb(void *arg)
{
    (void)arg;
    struct ble_gap_disc_params params = {};
    params.passive = 1;
    params.filter_duplicates = 0;
    params.itvl = BLE_GAP_SCAN_ITVL_MS(100);
    params.window = BLE_GAP_SCAN_WIN_MS(100);

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, CONFIG_BMU_VIC_SCAN_DURATION_S * 1000,
                          &params, scan_event_handler, NULL);
    if (rc == 0) {
        ESP_LOGD(TAG, "Scan started (%ds)", CONFIG_BMU_VIC_SCAN_DURATION_S);
    }
}

/* ── Public API ───────────────────────────────────────────────────── */

esp_err_t bmu_vic_scan_init(void)
{
    s_device_count = 0;
    memset(s_devices, 0, sizeof(s_devices));
    ESP_LOGI(TAG, "Init OK — scan %ds every %ds, max %d devices",
             CONFIG_BMU_VIC_SCAN_DURATION_S, CONFIG_BMU_VIC_SCAN_PERIOD_S,
             CONFIG_BMU_VIC_SCAN_MAX_DEVICES);
    return ESP_OK;
}

esp_err_t bmu_vic_scan_start(void)
{
    if (s_scan_timer != NULL) return ESP_OK;
    const esp_timer_create_args_t args = {
        .callback = scan_timer_cb, .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK, .name = "vic_scan",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &s_scan_timer);
    esp_timer_start_periodic(s_scan_timer, CONFIG_BMU_VIC_SCAN_PERIOD_S * 1000000ULL);
    scan_timer_cb(NULL); /* First scan immediately */
    return ESP_OK;
}

void bmu_vic_scan_stop(void)
{
    if (s_scan_timer) {
        esp_timer_stop(s_scan_timer);
        esp_timer_delete(s_scan_timer);
        s_scan_timer = NULL;
    }
}

int bmu_vic_scan_get_devices(bmu_vic_device_t *out, int max)
{
    int count = 0;
    int64_t now = now_ms();
    for (int i = 0; i < s_device_count && count < max; i++) {
        if ((now - s_devices[i].last_seen_ms) < EXPIRY_MS) {
            out[count++] = s_devices[i];
        }
    }
    return count;
}

const bmu_vic_device_t *bmu_vic_scan_get_device_by_mac(const uint8_t mac[6])
{
    for (int i = 0; i < s_device_count; i++) {
        if (memcmp(s_devices[i].mac, mac, 6) == 0) return &s_devices[i];
    }
    return NULL;
}

int bmu_vic_scan_count(void)
{
    int count = 0;
    int64_t now = now_ms();
    for (int i = 0; i < s_device_count; i++) {
        if ((now - s_devices[i].last_seen_ms) < EXPIRY_MS) count++;
    }
    return count;
}

#endif /* CONFIG_BMU_VIC_SCAN_ENABLED */
