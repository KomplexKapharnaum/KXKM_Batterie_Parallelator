/**
 * @file bmu_display.cpp
 * @brief Display init for ESP32-S3-BOX-3 — LVGL v9 tabview with 5 screens.
 *
 * Ecrans : Batteries | SOH | Systeme | Alertes | Config
 * Navigation : swipe horizontal ou tap sur les onglets en bas.
 * Tap sur une cellule batterie → ecran detail en overlay.
 */

#include "bmu_display.h"
#include "bmu_ui.h"
#include "bmu_vedirect.h"
#include "bmu_climate.h"
#include "bmu_ina237.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
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

/* ── Runtime UI context sync ───────────────────────────────────────── */

static uint8_t visible_battery_count(void)
{
    /* Source of truth = protection context (updated live by hotplug + protection task) */
    uint8_t count = 0;
    if (s_ui_ctx.prot != NULL) {
        count = s_ui_ctx.prot->nb_ina;
    } else if (s_ctx != NULL && s_ctx->nb_ina_ptr != NULL) {
        count = *s_ctx->nb_ina_ptr;
    }

    if (count > BMU_MAX_BATTERIES) {
        count = BMU_MAX_BATTERIES;
    }
    return count;
}

static void sync_ui_runtime_state(void)
{
    /* Keep display count aligned with the live protection/manager state.
     * This prevents the UI from rendering batteries that are not actually
     * backed by initialized sensor/state objects yet. */
    s_ui_ctx.nb_ina = visible_battery_count();
}

/* ── Backlight ──────────────────────────────────────────────────────── */

static void backlight_set_full(void)
{
    bsp_display_brightness_set(100);
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
    if (s_ui_ctx.chart_hist == NULL || s_ui_ctx.mgr == NULL) return;

    const int nb = visible_battery_count();
    for (int i = 0; i < nb; i++) {
        float v_mv = bmu_battery_manager_get_last_voltage_mv(s_ui_ctx.mgr, i);
        if (v_mv <= 1000.0f && s_ui_ctx.prot != NULL) {
            v_mv = bmu_protection_get_voltage(s_ui_ctx.prot, i);
        }
        float i_a = bmu_battery_manager_get_last_current_a(s_ui_ctx.mgr, i);
        bmu_chart_history_push(&s_ui_ctx.chart_hist[i], v_mv, i_a);
    }
}

/* ── Periodic update (timer callback) ──────────────────────────────── */

static void display_periodic_cb(void *arg)
{
    (void)arg;
    backlight_check_dim();

    if (s_disp == NULL || !s_ui_ready) return;

    sync_ui_runtime_state();

    /* Push chart data toutes les 500ms */
    s_chart_push_counter++;
    if (s_chart_push_counter >= CHART_PUSH_INTERVAL) {
        s_chart_push_counter = 0;
        chart_history_push_all();
    }

    if (bsp_display_lock(0)) {
        /* bmu_ui_main_update gere : grille OU detail selon nav state */
        bmu_ui_main_update(&s_ui_ctx);
        bmu_ui_soh_update(&s_ui_ctx);
        bmu_ui_system_update(&s_ui_ctx);
        /* alerts update on demand only */
        bmu_ui_config_update();

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
    sync_ui_runtime_state();
    s_ui_ready = false;

    /* Init navigation state */
    s_nav.detail_visible = false;
    s_nav.detail_battery = -1;
    s_nav.detail_panel = NULL;

    /* Allocate chart history in PSRAM (32 × 4.8KB = 153KB) */
    if (s_ui_ctx.chart_hist == NULL) {
        s_ui_ctx.chart_hist = (bmu_chart_history_t *)heap_caps_calloc(
            BMU_MAX_BATTERIES, sizeof(bmu_chart_history_t), MALLOC_CAP_SPIRAM);
        if (s_ui_ctx.chart_hist == NULL) {
            ESP_LOGE(TAG, "chart_hist PSRAM alloc failed!");
        }
    }

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

    /* ── Fond noir pur ────────────────────────────────────────────── */
    lv_obj_set_style_bg_color(lv_scr_act(), UI_COLOR_BG, 0);

    /* ── Tabview — onglets en bas, 5 ecrans ───────────────────────── */
    s_tabview = lv_tabview_create(lv_scr_act());
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(s_tabview, 30);
    lv_obj_set_size(s_tabview, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_obj_set_style_bg_color(s_tabview, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(s_tabview, 0, 0);

    /* Style tab bar */
    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(s_tabview);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_border_width(tab_bar, 0, 0);
    lv_obj_set_style_pad_all(tab_bar, 0, 0);

    /* Creer les 5 onglets */
    lv_obj_t *tab_batt   = lv_tabview_add_tab(s_tabview, LV_SYMBOL_CHARGE " Batt");
    lv_obj_t *tab_soh    = lv_tabview_add_tab(s_tabview, LV_SYMBOL_EYE_OPEN " SOH");
    lv_obj_t *tab_sys    = lv_tabview_add_tab(s_tabview, LV_SYMBOL_SETTINGS " Sys");
    lv_obj_t *tab_alerts = lv_tabview_add_tab(s_tabview, LV_SYMBOL_WARNING " Alert");
    lv_obj_t *tab_config = lv_tabview_add_tab(s_tabview, LV_SYMBOL_EDIT " Config");

    /* Fond noir pour chaque onglet */
    lv_obj_t *tabs[] = {tab_batt, tab_soh, tab_sys, tab_alerts, tab_config};
    for (int i = 0; i < 5; i++) {
        lv_obj_set_style_bg_color(tabs[i], UI_COLOR_BG, 0);
    }

    /* ── Creer le contenu de chaque ecran ─────────────────────────── */
    /* Battery + SOH rows created lazily in update() — fast init */
    bmu_ui_main_set_nav_state(&s_nav);
    bmu_ui_detail_set_nav_state(&s_nav);
    bmu_ui_main_create(tab_batt, &s_ui_ctx);
    bmu_ui_soh_create(tab_soh, &s_ui_ctx);
    bmu_ui_system_create(tab_sys, &s_ui_ctx);
    bmu_ui_alerts_create(tab_alerts);
    bmu_ui_config_create(tab_config);

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

    /* Periodic timer NOT started yet — call bmu_display_start_updates()
     * AFTER protection task is running so the first callback sees
     * nb_ina != 0 and creates the battery rows immediately. */

    ESP_LOGI(TAG, "=== Affichage BMU pret (HW + UI crees, updates en attente) ===");
    return ESP_OK;
}

esp_err_t bmu_display_start_updates(void)
{
    if (s_periodic_timer != NULL) return ESP_OK;  /* already started */

    const esp_timer_create_args_t timer_args = {
        .callback = display_periodic_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "disp_periodic",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(
        s_periodic_timer,
        CONFIG_BMU_DISPLAY_REFRESH_MS * 1000ULL));

    ESP_LOGI(TAG, "=== Display updates started (refresh=%dms, dim=%ds, chart=%d pts) ===",
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
