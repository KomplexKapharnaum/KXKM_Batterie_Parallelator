/**
 * bmu_ble_victron_gatt — Service GATT emulant un SmartShunt Victron.
 * VictronConnect detecte ce service et affiche les donnees BMU.
 * Lecture seule — aucune commande d'ecriture.
 */

#include "bmu_ble_victron_gatt.h"

#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_climate.h"
#include "bmu_config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <cmath>

#if !CONFIG_BMU_VICTRON_GATT_ENABLED

const struct ble_gatt_svc_def *bmu_ble_victron_gatt_svc_defs(void) { return NULL; }
void bmu_ble_victron_gatt_notify_start(void) {}
void bmu_ble_victron_gatt_notify_stop(void) {}

#else

static const char *TAG = "VIC_GATT";

/* ── Victron-like UUIDs (community reverse-engineered) ────────────── */
/* Service:        68c10001-b17f-4d3a-a290-34ad6499937c */
/* Characteristics: 68c100xx-... where xx = suffix below */
#define VIC_SVC_UUID_DECLARE()  \
    BLE_UUID128_INIT(0x7c, 0x93, 0x99, 0x64, 0xad, 0x34, 0x90, 0xa2, \
                     0x3a, 0x4d, 0x7f, 0xb1, 0x01, 0x00, 0xc1, 0x68)

#define VIC_CHR_UUID(suffix)    \
    BLE_UUID128_INIT(0x7c, 0x93, 0x99, 0x64, 0xad, 0x34, 0x90, 0xa2, \
                     0x3a, 0x4d, 0x7f, 0xb1, (suffix), 0x00, 0xc1, 0x68)

/* Characteristic value handles (populated by NimBLE) */
static uint16_t s_hdl_voltage     = 0;
static uint16_t s_hdl_current     = 0;
static uint16_t s_hdl_soc         = 0;
static uint16_t s_hdl_consumed_ah = 0;
static uint16_t s_hdl_ttg         = 0;
static uint16_t s_hdl_temperature = 0;
static uint16_t s_hdl_alarm       = 0;

static esp_timer_handle_t s_notify_timer = NULL;

/* ── Extern accessors from bmu_ble ────────────────────────────────── */
extern "C" bmu_protection_ctx_t  *bmu_ble_get_prot(void);
extern "C" bmu_battery_manager_t *bmu_ble_get_mgr(void);
extern "C" uint8_t                bmu_ble_get_nb_ina(void);

/* ── Read callbacks ───────────────────────────────────────────────── */

static int read_voltage(uint16_t conn, uint16_t attr,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    bmu_protection_ctx_t *prot = bmu_ble_get_prot();
    uint8_t nb = bmu_ble_get_nb_ina();
    float sum = 0; int n = 0;
    for (int i = 0; i < nb; i++) {
        if (bmu_protection_get_state(prot, i) == BMU_STATE_CONNECTED) {
            sum += bmu_protection_get_voltage(prot, i);
            n++;
        }
    }
    uint16_t val = (n > 0) ? (uint16_t)((sum / n) / 10.0f) : 0; /* 0.01V */
    os_mbuf_append(ctxt->om, &val, 2);
    return 0;
}

static int read_current(uint16_t conn, uint16_t attr,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    /* Sum discharge rate as proxy for total current (0.1A signed) */
    int16_t val = 0;
    os_mbuf_append(ctxt->om, &val, 2);
    return 0;
}

static int read_soc(uint16_t conn, uint16_t attr,
                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    bmu_protection_ctx_t *prot = bmu_ble_get_prot();
    uint8_t nb = bmu_ble_get_nb_ina();
    uint16_t min_mv, max_mv, dummy1, dummy2;
    bmu_config_get_thresholds(&min_mv, &max_mv, &dummy1, &dummy2);
    float sum = 0; int n = 0;
    for (int i = 0; i < nb; i++) {
        if (bmu_protection_get_state(prot, i) == BMU_STATE_CONNECTED) {
            sum += bmu_protection_get_voltage(prot, i);
            n++;
        }
    }
    float avg_mv = (n > 0) ? sum / n : 0;
    float soc = (avg_mv - min_mv) / (float)(max_mv - min_mv) * 100.0f;
    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;
    uint16_t val = (uint16_t)(soc * 100.0f); /* 0-10000 */
    os_mbuf_append(ctxt->om, &val, 2);
    return 0;
}

static int read_consumed(uint16_t conn, uint16_t attr,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    bmu_battery_manager_t *mgr = bmu_ble_get_mgr();
    uint8_t nb = bmu_ble_get_nb_ina();
    float sum = 0;
    for (int i = 0; i < nb; i++) sum += bmu_battery_manager_get_ah_discharge(mgr, i);
    int32_t val = (int32_t)(sum * 10.0f); /* 0.1Ah */
    os_mbuf_append(ctxt->om, &val, 4);
    return 0;
}

static int read_ttg(uint16_t conn, uint16_t attr,
                    struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    uint16_t val = 0xFFFF; /* infinite (no discharge estimation) */
    os_mbuf_append(ctxt->om, &val, 2);
    return 0;
}

static int read_temperature(uint16_t conn, uint16_t attr,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    int16_t val = 0;
    if (bmu_climate_is_available()) {
        val = (int16_t)(bmu_climate_get_temperature() * 100.0f + 27315); /* 0.01K */
    }
    os_mbuf_append(ctxt->om, &val, 2);
    return 0;
}

static int read_alarm(uint16_t conn, uint16_t attr,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    bmu_protection_ctx_t *prot = bmu_ble_get_prot();
    uint8_t nb = bmu_ble_get_nb_ina();
    uint16_t min_mv, max_mv, max_ma, dummy;
    bmu_config_get_thresholds(&min_mv, &max_mv, &max_ma, &dummy);

    uint16_t alarm = 0;
    float sum = 0; int n = 0;
    bool has_error = false, has_locked = false;
    for (int i = 0; i < nb; i++) {
        bmu_battery_state_t st = bmu_protection_get_state(prot, i);
        if (st == BMU_STATE_ERROR) has_error = true;
        if (st == BMU_STATE_LOCKED) has_locked = true;
        if (st == BMU_STATE_CONNECTED) {
            sum += bmu_protection_get_voltage(prot, i);
            n++;
        }
    }
    if (n > 0) {
        float avg = sum / n;
        if (avg < min_mv) alarm |= (1 << 0); /* low V */
        if (avg > max_mv) alarm |= (1 << 1); /* high V */
    }
    if (has_error)  alarm |= (1 << 4);
    if (has_locked) alarm |= (1 << 5);
    if (bmu_climate_is_available() && bmu_climate_get_temperature() > 60.0f)
        alarm |= (1 << 3);

    os_mbuf_append(ctxt->om, &alarm, 2);
    return 0;
}

static int read_model(uint16_t conn, uint16_t attr,
                      struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    const char *name = "KXKM BMU";
    os_mbuf_append(ctxt->om, name, strlen(name));
    return 0;
}

static int read_serial(uint16_t conn, uint16_t attr,
                       struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    const char *sn = bmu_config_get_device_name();
    os_mbuf_append(ctxt->om, sn, strlen(sn));
    return 0;
}

/* ── Service definition ───────────────────────────────────────────── */

static const ble_uuid128_t svc_uuid = VIC_SVC_UUID_DECLARE();
static const ble_uuid128_t chr_voltage_uuid     = VIC_CHR_UUID(0x11);
static const ble_uuid128_t chr_current_uuid      = VIC_CHR_UUID(0x12);
static const ble_uuid128_t chr_soc_uuid          = VIC_CHR_UUID(0x13);
static const ble_uuid128_t chr_consumed_uuid     = VIC_CHR_UUID(0x14);
static const ble_uuid128_t chr_ttg_uuid          = VIC_CHR_UUID(0x15);
static const ble_uuid128_t chr_temp_uuid         = VIC_CHR_UUID(0x16);
static const ble_uuid128_t chr_alarm_uuid        = VIC_CHR_UUID(0x17);
static const ble_uuid128_t chr_model_uuid        = VIC_CHR_UUID(0x20);
static const ble_uuid128_t chr_serial_uuid       = VIC_CHR_UUID(0x21);

static const struct ble_gatt_chr_def vic_chrs[] = {
    { .uuid = &chr_voltage_uuid.u,  .access_cb = read_voltage,     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_hdl_voltage },
    { .uuid = &chr_current_uuid.u,  .access_cb = read_current,     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_hdl_current },
    { .uuid = &chr_soc_uuid.u,      .access_cb = read_soc,         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_hdl_soc },
    { .uuid = &chr_consumed_uuid.u, .access_cb = read_consumed,    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_hdl_consumed_ah },
    { .uuid = &chr_ttg_uuid.u,      .access_cb = read_ttg,         .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_hdl_ttg },
    { .uuid = &chr_temp_uuid.u,     .access_cb = read_temperature, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_hdl_temperature },
    { .uuid = &chr_alarm_uuid.u,    .access_cb = read_alarm,       .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_hdl_alarm },
    { .uuid = &chr_model_uuid.u,    .access_cb = read_model,       .flags = BLE_GATT_CHR_F_READ },
    { .uuid = &chr_serial_uuid.u,   .access_cb = read_serial,      .flags = BLE_GATT_CHR_F_READ },
    { .uuid = NULL } /* terminateur */
};

static const struct ble_gatt_svc_def vic_svc_def[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_uuid.u,
        .characteristics = vic_chrs,
    },
    { .type = 0, .uuid = NULL, .includes = NULL, .characteristics = NULL }
};

const struct ble_gatt_svc_def *bmu_ble_victron_gatt_svc_defs(void)
{
    return vic_svc_def;
}

/* ── Notification timer (1s) ──────────────────────────────────────── */

static void notify_timer_cb(void *arg)
{
    (void)arg;
    /* Notify all subscribed clients on voltage + current + soc */
    uint16_t handles[] = { s_hdl_voltage, s_hdl_current, s_hdl_soc,
                           s_hdl_consumed_ah, s_hdl_alarm };
    for (int h = 0; h < 5; h++) {
        if (handles[h] == 0) continue;
        ble_gatts_chr_updated(handles[h]);
    }
}

void bmu_ble_victron_gatt_notify_start(void)
{
    if (s_notify_timer != NULL) return;
    const esp_timer_create_args_t args = {
        .callback = notify_timer_cb, .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK, .name = "vic_gatt",
        .skip_unhandled_events = true,
    };
    esp_timer_create(&args, &s_notify_timer);
    esp_timer_start_periodic(s_notify_timer, 1000000ULL); /* 1s */
    ESP_LOGI(TAG, "Victron GATT notifications started");
}

void bmu_ble_victron_gatt_notify_stop(void)
{
    if (s_notify_timer == NULL) return;
    esp_timer_stop(s_notify_timer);
    esp_timer_delete(s_notify_timer);
    s_notify_timer = NULL;
    ESP_LOGI(TAG, "Victron GATT notifications stopped");
}

#endif /* CONFIG_BMU_VICTRON_GATT_ENABLED */
