#include "bmu_ui.h"
#include "bmu_wifi.h"
#include "bmu_mqtt.h"
#include "bmu_ble.h"
#include "bmu_climate.h"
#include "bmu_vedirect.h"
#include "bmu_storage.h"
#include "bmu_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char *TAG = "UI_SYS";

/* ── Ring buffer pour les messages debug ─────────────────────────────── */
#define DEBUG_LOG_MAX 30
#define DEBUG_MSG_LEN 48

static char debug_log[DEBUG_LOG_MAX][DEBUG_MSG_LEN] = {};
static int debug_log_count = 0;
static int debug_log_head = 0; // prochaine position d'ecriture
static uint32_t error_count = 0;
static uint32_t nack_count = 0;
static uint32_t timeout_count = 0;
static int device_count = 0;

void bmu_ui_debug_log(const char *msg)
{
    if (msg == NULL) return;

    /* Horodatage depuis le boot */
    int64_t ms = esp_timer_get_time() / 1000;
    int sec = (int)(ms / 1000) % 86400;
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;

    snprintf(debug_log[debug_log_head], DEBUG_MSG_LEN,
             "%02d:%02d:%02d %s", h, m, s, msg);
    debug_log_head = (debug_log_head + 1) % DEBUG_LOG_MAX;
    if (debug_log_count < DEBUG_LOG_MAX) debug_log_count++;
}

void bmu_ui_debug_log_i2c_error(uint8_t addr, const char *type)
{
    char buf[40];
    snprintf(buf, sizeof(buf), "%s 0x%02X", type, addr);
    bmu_ui_debug_log(buf);

    if (strcmp(type, "NACK") == 0) nack_count++;
    else if (strcmp(type, "TIMEOUT") == 0) timeout_count++;
    error_count++;
}

void bmu_ui_debug_set_device_count(int count)
{
    device_count = count;
}

const char *bmu_ui_debug_get_log_line(int index)
{
    if (index < 0 || index >= debug_log_count) return NULL;
    int idx = (debug_log_head - 1 - index + DEBUG_LOG_MAX) % DEBUG_LOG_MAX;
    return debug_log[idx];
}

int bmu_ui_debug_get_device_count(void) { return device_count; }
int bmu_ui_debug_get_error_count(void) { return (int)error_count; }

/* Section CONNEXION */
static lv_obj_t *s_ble_dot  = NULL, *s_ble_lbl  = NULL;
static lv_obj_t *s_wifi_dot = NULL, *s_wifi_lbl = NULL;
static lv_obj_t *s_mqtt_dot = NULL, *s_mqtt_lbl = NULL;

/* Section CLIMAT */
static lv_obj_t *s_temp_lbl = NULL;
static lv_obj_t *s_hum_lbl  = NULL;

/* Section SOLAIRE */
static lv_obj_t *s_solar_container = NULL;
static lv_obj_t *s_mppt_state      = NULL;
static lv_obj_t *s_pv_info         = NULL;
static lv_obj_t *s_yield_info      = NULL;

/* Section FIRMWARE */
static lv_obj_t *s_fw_info = NULL;

/* Section I2C */
static lv_obj_t *s_i2c_info    = NULL;
static lv_obj_t *s_i2c_log[3]  = {};

static bmu_ui_ctx_t *s_ctx = NULL;

/* ── MPPT state color helper ──────────────────────────────────── */

static lv_color_t cs_color(uint8_t cs)
{
    switch (cs) {
        case 0:  return UI_COLOR_TEXT_DIM;  /* Off          */
        case 3:  return UI_COLOR_WARN;      /* Bulk         */
        case 4:  return UI_COLOR_SOLAR;     /* Absorption   */
        case 5:  return UI_COLOR_OK;        /* Float        */
        case 7:  return UI_COLOR_INFO;      /* Equalize     */
        default: return UI_COLOR_TEXT_DIM;
    }
}

/* ── Section title helper ─────────────────────────────────────── */

static lv_obj_t *make_section_title(lv_obj_t *parent, const char *text, lv_coord_t y)
{
    lv_obj_t *lbl = (lv_obj_t *)lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_opa(lbl, LV_OPA_50, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 4, y);
    return lbl;
}

/* ── Dot indicator helper ─────────────────────────────────────── */

static lv_obj_t *make_dot(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *dot = (lv_obj_t *)lv_obj_create(parent);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_color(dot, UI_COLOR_TEXT_DIM, 0);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, x, y);
    return dot;
}

/* ── bmu_ui_system_create ──────────────────────────────────────── */

void bmu_ui_system_create(lv_obj_t *parent, bmu_ui_ctx_t *ctx)
{
    s_ctx = ctx;

    lv_obj_set_style_bg_color(parent, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

    /* ── Titre de l'onglet ──────────────────────────────────────── */
    lv_obj_t *title = (lv_obj_t *)lv_label_create(parent);
    lv_label_set_text(title, "SYSTEME");
    lv_obj_set_style_text_color(title, UI_COLOR_INFO, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    /* ═══════════════════════════════════════════════════════════
     * Section 1 — CONNEXION  (y=20, hauteur ~20px)
     * ═══════════════════════════════════════════════════════════ */
    make_section_title(parent, "CONNEXION", 20);

    /* BLE */
    s_ble_dot = make_dot(parent, 4, 36);
    s_ble_lbl = (lv_obj_t *)lv_label_create(parent);
    lv_label_set_text(s_ble_lbl, "BLE");
    lv_obj_set_style_text_color(s_ble_lbl, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_font(s_ble_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_ble_lbl, LV_ALIGN_TOP_LEFT, 16, 34);

    /* WiFi */
    s_wifi_dot = make_dot(parent, 60, 36);
    s_wifi_lbl = (lv_obj_t *)lv_label_create(parent);
    lv_label_set_text(s_wifi_lbl, "---");
    lv_obj_set_style_text_color(s_wifi_lbl, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_font(s_wifi_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_wifi_lbl, LV_ALIGN_TOP_LEFT, 72, 34);

    /* MQTT */
    s_mqtt_dot = make_dot(parent, 200, 36);
    s_mqtt_lbl = (lv_obj_t *)lv_label_create(parent);
    lv_label_set_text(s_mqtt_lbl, "MQTT");
    lv_obj_set_style_text_color(s_mqtt_lbl, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_font(s_mqtt_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_mqtt_lbl, LV_ALIGN_TOP_LEFT, 212, 34);

    /* ═══════════════════════════════════════════════════════════
     * Section 2 — CLIMAT  (y=58, hauteur ~20px)
     * ═══════════════════════════════════════════════════════════ */
    make_section_title(parent, "CLIMAT", 58);

    s_temp_lbl = (lv_obj_t *)lv_label_create(parent);
    lv_label_set_text(s_temp_lbl, "---");
    lv_obj_set_style_text_color(s_temp_lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_temp_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_temp_lbl, LV_ALIGN_TOP_LEFT, 80, 56);

    s_hum_lbl = (lv_obj_t *)lv_label_create(parent);
    lv_label_set_text(s_hum_lbl, "---");
    lv_obj_set_style_text_color(s_hum_lbl, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_font(s_hum_lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(s_hum_lbl, LV_ALIGN_TOP_LEFT, 200, 56);

    /* ═══════════════════════════════════════════════════════════
     * Section 3 — SOLAIRE  (y=82, hauteur ~60px)
     * ═══════════════════════════════════════════════════════════ */
    make_section_title(parent, "SOLAIRE", 82);

    s_solar_container = (lv_obj_t *)lv_obj_create(parent);
    lv_obj_set_size(s_solar_container, 316, 58);
    lv_obj_set_style_bg_opa(s_solar_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_solar_container, 0, 0);
    lv_obj_set_style_pad_all(s_solar_container, 0, 0);
    lv_obj_align(s_solar_container, LV_ALIGN_TOP_LEFT, 2, 96);

    s_mppt_state = (lv_obj_t *)lv_label_create(s_solar_container);
    lv_label_set_text(s_mppt_state, "---");
    lv_obj_set_style_text_font(s_mppt_state, &lv_font_montserrat_14, 0);
    lv_obj_align(s_mppt_state, LV_ALIGN_TOP_LEFT, 0, 0);

    s_pv_info = (lv_obj_t *)lv_label_create(s_solar_container);
    lv_label_set_text(s_pv_info, "---");
    lv_obj_set_style_text_color(s_pv_info, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_font(s_pv_info, &lv_font_montserrat_14, 0);
    lv_obj_align(s_pv_info, LV_ALIGN_TOP_LEFT, 0, 20);

    s_yield_info = (lv_obj_t *)lv_label_create(s_solar_container);
    lv_label_set_text(s_yield_info, "---");
    lv_obj_set_style_text_color(s_yield_info, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_font(s_yield_info, &lv_font_montserrat_14, 0);
    lv_obj_align(s_yield_info, LV_ALIGN_TOP_LEFT, 0, 40);

    /* ═══════════════════════════════════════════════════════════
     * Section 4 — FIRMWARE  (y=158, hauteur ~20px)
     * ═══════════════════════════════════════════════════════════ */
    make_section_title(parent, "FIRMWARE", 158);

    s_fw_info = (lv_obj_t *)lv_label_create(parent);
    lv_label_set_text(s_fw_info, "---");
    lv_obj_set_style_text_color(s_fw_info, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_font(s_fw_info, &lv_font_montserrat_14, 0);
    lv_obj_align(s_fw_info, LV_ALIGN_TOP_LEFT, 80, 156);

    /* ═══════════════════════════════════════════════════════════
     * Section 5 — I2C BUS  (y=182, hauteur ~40px)
     * ═══════════════════════════════════════════════════════════ */
    make_section_title(parent, "I2C BUS", 182);

    s_i2c_info = (lv_obj_t *)lv_label_create(parent);
    lv_label_set_text(s_i2c_info, "---");
    lv_obj_set_style_text_color(s_i2c_info, UI_COLOR_TEXT_SEC, 0);
    lv_obj_set_style_text_font(s_i2c_info, &lv_font_montserrat_14, 0);
    lv_obj_align(s_i2c_info, LV_ALIGN_TOP_LEFT, 80, 180);

    for (int i = 0; i < 3; i++) {
        s_i2c_log[i] = (lv_obj_t *)lv_label_create(parent);
        lv_label_set_text(s_i2c_log[i], "");
        lv_obj_set_style_text_color(s_i2c_log[i], UI_COLOR_TEXT_DIM, 0);
        lv_obj_set_style_text_font(s_i2c_log[i], &lv_font_montserrat_14, 0);
        lv_obj_align(s_i2c_log[i], LV_ALIGN_TOP_LEFT, 4, 196 + i * 14);
    }

    ESP_LOGI(TAG, "system screen created");
}

/* ── bmu_ui_system_update ──────────────────────────────────────── */

void bmu_ui_system_update(bmu_ui_ctx_t *ctx)
{
    char buf[64];

    /* ── Section 1 — CONNEXION ─────────────────────────────────── */

    /* BLE */
    bool ble_ok = bmu_ble_is_connected();
    lv_obj_set_style_bg_color(s_ble_dot,
        ble_ok ? UI_COLOR_OK : UI_COLOR_TEXT_DIM, 0);

    /* WiFi */
    bool wifi_ok = bmu_wifi_is_connected();
    lv_obj_set_style_bg_color(s_wifi_dot,
        wifi_ok ? UI_COLOR_OK : UI_COLOR_TEXT_DIM, 0);
    if (wifi_ok) {
        char ip[20] = {};
        bmu_wifi_get_ip(ip, sizeof(ip));
        lv_label_set_text(s_wifi_lbl, ip);
    } else {
        lv_label_set_text(s_wifi_lbl, "WiFi");
    }

    /* MQTT */
    bool mqtt_ok = bmu_mqtt_is_connected();
    lv_obj_set_style_bg_color(s_mqtt_dot,
        mqtt_ok ? UI_COLOR_OK : UI_COLOR_WARN, 0);

    /* ── Section 2 — CLIMAT ────────────────────────────────────── */

    if (bmu_climate_is_available()) {
        snprintf(buf, sizeof(buf), "%.1f °C", bmu_climate_get_temperature());
        lv_label_set_text(s_temp_lbl, buf);
        snprintf(buf, sizeof(buf), "%.0f %%", bmu_climate_get_humidity());
        lv_label_set_text(s_hum_lbl, buf);
    } else {
        lv_label_set_text(s_temp_lbl, "---");
        lv_label_set_text(s_hum_lbl, "---");
    }

    /* ── Section 3 — SOLAIRE ───────────────────────────────────── */

    if (bmu_vedirect_is_connected()) {
        lv_obj_clear_flag(s_solar_container, LV_OBJ_FLAG_HIDDEN);
        const bmu_vedirect_data_t *vd = bmu_vedirect_get_data();
        if (vd && vd->valid) {
            /* MPPT state avec couleur */
            lv_label_set_text(s_mppt_state, bmu_vedirect_cs_name(vd->charge_state));
            lv_obj_set_style_text_color(s_mppt_state, cs_color(vd->charge_state), 0);

            /* PV tension + puissance + tension batterie */
            snprintf(buf, sizeof(buf), "PV %.1fV  %dW  Batt %.2fV",
                     vd->panel_voltage_v,
                     vd->panel_power_w,
                     vd->battery_voltage_v);
            lv_label_set_text(s_pv_info, buf);

            /* Rendement du jour */
            snprintf(buf, sizeof(buf), "Yield today: %lu Wh  Max: %dW",
                     (unsigned long)vd->yield_today_wh,
                     vd->max_power_today_w);
            lv_label_set_text(s_yield_info, buf);
        } else {
            lv_label_set_text(s_mppt_state, "...");
            lv_obj_set_style_text_color(s_mppt_state, UI_COLOR_TEXT_DIM, 0);
            lv_label_set_text(s_pv_info, "");
            lv_label_set_text(s_yield_info, "");
        }
    } else {
        lv_obj_add_flag(s_solar_container, LV_OBJ_FLAG_HIDDEN);
    }

    /* ── Section 4 — FIRMWARE ──────────────────────────────────── */

    {
        const esp_app_desc_t *desc = esp_app_get_description();
        uint32_t heap_kb = esp_get_free_heap_size() / 1024;
        uint32_t secs    = (uint32_t)(esp_timer_get_time() / 1000000ULL);
        uint32_t h       = secs / 3600;
        uint32_t m       = (secs % 3600) / 60;
        int nb_ina = ctx ? (int)ctx->nb_ina : 0;

        snprintf(buf, sizeof(buf), "v%.8s %lukB %luh%02lum %dI",
                 desc->version,
                 (unsigned long)heap_kb,
                 (unsigned long)h,
                 (unsigned long)m,
                 nb_ina);
        lv_label_set_text(s_fw_info, buf);
    }

    /* ── Section 5 — I2C BUS ───────────────────────────────────── */

    {
        int dev_count = bmu_ui_debug_get_device_count();
        int err_count = bmu_ui_debug_get_error_count();
        snprintf(buf, sizeof(buf), "%d dev  %d err", dev_count, err_count);
        lv_label_set_text(s_i2c_info, buf);

        for (int i = 0; i < 3; i++) {
            const char *line = bmu_ui_debug_get_log_line(i);
            lv_label_set_text(s_i2c_log[i], line ? line : "");
        }
    }
}
