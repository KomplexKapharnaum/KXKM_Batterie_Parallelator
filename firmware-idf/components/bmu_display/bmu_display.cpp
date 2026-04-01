/**
 * @file bmu_display.cpp
 * @brief Display init for ESP32-S3-BOX-3 — LVGL v9 tabview with 5 screens.
 *
 * Ecrans : Batteries | Systeme | Alertes | Debug I2C | Solar
 * Navigation : swipe horizontal ou tap sur les onglets en bas.
 * Tap sur une cellule batterie → ecran detail en overlay.
 */

#include "bmu_display.h"
#include "bmu_ui.h"
#include "bmu_vedirect.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include <cstring>

static const char *TAG = "DISP";

static bmu_display_ctx_t *s_ctx = NULL;
static bmu_ui_ctx_t s_ui_ctx = {};
static bmu_nav_state_t s_nav = {};
static lv_display_t *s_disp = NULL;
static lv_obj_t *s_tabview = NULL;
static int64_t s_last_touch_us = 0;
static bool s_dimmed = false;
static bool s_update_req = false;
static bool s_ui_ready = false;
static esp_timer_handle_t s_periodic_timer = NULL;

/* Compteur pour le push chart (500ms = toutes les 5 iterations @ 100ms refresh) */
static int s_chart_push_counter = 0;
#define CHART_PUSH_INTERVAL  5  // 5 * 100ms = 500ms

/* ── Backlight ──────────────────────────────────────────────────────── */

static void backlight_set_full(void)
{
    bsp_display_brightness_set(100);
    bsp_display_backlight_on();
    s_dimmed = false;
}

static void backlight_check_dim(void)
{
    const uint32_t timeout_s = CONFIG_BMU_DISPLAY_BL_DIM_TIMEOUT_S;
    if (timeout_s == 0) return;

    const int64_t now = esp_timer_get_time();
    if (!s_dimmed && (now - s_last_touch_us) > (int64_t)timeout_s * 1000000LL) {
        bsp_display_brightness_set(12);
        s_dimmed = true;
    }
}

/* ── Chart history push (500ms) ────────────────────────────────────── */

static void chart_history_push_all(void)
{
    int nb = s_ui_ctx.nb_ina > 16 ? 16 : s_ui_ctx.nb_ina;
    for (int i = 0; i < nb; i++) {
        float v_mv = bmu_protection_get_voltage(s_ui_ctx.prot, i);
        float i_a = 0.0f; // TODO: lire depuis INA quand API disponible
        bmu_chart_history_push(&s_ui_ctx.chart_hist[i], v_mv, i_a);
    }
}

/* ── Periodic update (timer callback) ──────────────────────────────── */

static void display_periodic_cb(void *arg)
{
    (void)arg;
    backlight_check_dim();

    if (s_disp == NULL || !s_ui_ready) return;

    /* Push chart data toutes les 500ms */
    s_chart_push_counter++;
    if (s_chart_push_counter >= CHART_PUSH_INTERVAL) {
        s_chart_push_counter = 0;
        chart_history_push_all();
    }

    if (bsp_display_lock(0)) {
        /* bmu_ui_main_update gere : grille OU detail selon nav state */
        bmu_ui_main_update(&s_ui_ctx);
        bmu_ui_system_update(&s_ui_ctx);
        bmu_ui_debug_update();
        bmu_ui_solar_update();
        bmu_ui_soh_update(&s_ui_ctx);

        if (s_update_req) {
            s_update_req = false;
            lv_obj_invalidate(lv_scr_act());
        }
        bsp_display_unlock();
    }
}

/* ── Public API ────────────────────────────────────────────────────── */

esp_err_t bmu_display_init(bmu_display_ctx_t *ctx)
{
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Contexte display NULL");
        return ESP_ERR_INVALID_ARG;
    }

    s_ctx = ctx;
    s_ui_ctx.prot = ctx->prot;
    s_ui_ctx.mgr = ctx->mgr;
    s_ui_ctx.nb_ina = ctx->nb_ina;
    s_ui_ready = false;

    /* Init navigation state */
    s_nav.detail_visible = false;
    s_nav.detail_battery = -1;
    s_nav.detail_panel = NULL;

    /* Init chart history (zero) */
    memset(s_ui_ctx.chart_hist, 0, sizeof(s_ui_ctx.chart_hist));

    ESP_LOGI(TAG, "=== Initialisation affichage BMU via BSP BOX-3 ===");

    /* Init display via BSP — utiliser PSRAM pour le buffer LVGL
     * (la RAM interne est trop petite pour un framebuffer 320x240x2=150KB) */
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_H_RES * 20, /* 20 lignes = 12.8 KB — petit buffer en RAM interne */
        .double_buffer = 0,
        .flags = {
            .buff_dma = true,      /* DMA en RAM interne */
            .buff_spiram = false,  /* Pas PSRAM (DMA SPI incompatible) */
        }
    };
    s_disp = bsp_display_start_with_config(&disp_cfg);
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start a echoue");
        return ESP_FAIL;
    }

    if (!bsp_display_lock(0)) {
        ESP_LOGE(TAG, "Impossible de prendre le lock LVGL");
        return ESP_FAIL;
    }

    /* ── Dark background ──────────────────────────────────────────── */
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1E1E1E), 0);

    /* ── Tabview — onglets en bas, 5 ecrans ───────────────────────── */
    s_tabview = lv_tabview_create(lv_scr_act());
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(s_tabview, 30);
    lv_obj_set_size(s_tabview, BSP_LCD_H_RES, BSP_LCD_V_RES);

    /* Style tabview : fond sombre, onglets discrets */
    lv_obj_set_style_bg_color(s_tabview, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_border_width(s_tabview, 0, 0);

    /* Style tab bar */
    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(s_tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(tab_bar, 0, 0);
    lv_obj_set_style_pad_all(tab_bar, 0, 0);

    /* Creer les 5 onglets */
    lv_obj_t *tab_batt   = lv_tabview_add_tab(s_tabview, LV_SYMBOL_CHARGE " Batt");
    lv_obj_t *tab_sys    = lv_tabview_add_tab(s_tabview, LV_SYMBOL_SETTINGS " Sys");
    lv_obj_t *tab_alerts = lv_tabview_add_tab(s_tabview, LV_SYMBOL_WARNING " Alert");
    lv_obj_t *tab_debug  = lv_tabview_add_tab(s_tabview, LV_SYMBOL_EYE_OPEN " I2C");
    lv_obj_t *tab_solar  = lv_tabview_add_tab(s_tabview, LV_SYMBOL_CHARGE " Solar");
    lv_obj_t *tab_soh    = lv_tabview_add_tab(s_tabview, LV_SYMBOL_OK " SOH");

    /* Fond sombre pour chaque onglet */
    lv_obj_set_style_bg_color(tab_batt,   lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_color(tab_sys,    lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_color(tab_alerts, lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_color(tab_debug,  lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_color(tab_solar,  lv_color_hex(0x1E1E1E), 0);
    lv_obj_set_style_bg_color(tab_soh,    lv_color_hex(0x1E1E1E), 0);

    /* ── Creer le contenu de chaque ecran ─────────────────────────── */
    bmu_ui_main_set_nav_state(&s_nav);
    bmu_ui_main_create(tab_batt, &s_ui_ctx);
    bmu_ui_system_create(tab_sys, &s_ui_ctx);
    bmu_ui_alerts_create(tab_alerts);
    bmu_ui_debug_create(tab_debug);
    bmu_ui_solar_create(tab_solar);
    bmu_ui_soh_create(tab_soh, &s_ui_ctx);

    s_ui_ready = true;
    bsp_display_unlock();

    /* ── Touch wakeup ─────────────────────────────────────────────── */
    s_last_touch_us = esp_timer_get_time();
    backlight_set_full();

    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev != NULL) {
        lv_indev_add_event_cb(indev, [](lv_event_t *e) {
            (void)e;
            bmu_display_wake();
        }, LV_EVENT_PRESSING, NULL);
    }

    /* ── Periodic timer for updates ───────────────────────────────── */
    if (s_periodic_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = display_periodic_cb,
            .arg = NULL,
            .name = "disp_periodic",
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_periodic_timer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(
            s_periodic_timer,
            CONFIG_BMU_DISPLAY_REFRESH_MS * 1000ULL));
    }

    ESP_LOGI(TAG, "=== Affichage BMU pret (refresh=%dms, dim=%ds, chart=%d pts) ===",
             CONFIG_BMU_DISPLAY_REFRESH_MS,
             CONFIG_BMU_DISPLAY_BL_DIM_TIMEOUT_S,
             CONFIG_BMU_CHART_HISTORY_POINTS);
    return ESP_OK;
}

void bmu_display_request_update(void)
{
    s_update_req = true;
}

void bmu_display_wake(void)
{
    s_last_touch_us = esp_timer_get_time();
    if (s_dimmed) {
        backlight_set_full();
    }
}

lv_obj_t *bmu_display_get_screen_container(void)
{
    return s_tabview;
}
