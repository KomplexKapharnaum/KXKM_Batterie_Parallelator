/**
 * @file bmu_display.cpp
 * @brief Display init for ESP32-S3-BOX-3 using official BSP APIs.
 */

#include "bmu_display.h"
#include "bmu_ui.h"

#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "DISP";

static bmu_display_ctx_t *s_ctx = NULL;
static bmu_ui_ctx_t s_ui_ctx = {};
static lv_display_t *s_disp = NULL;
static lv_obj_t *s_screen = NULL;
static int64_t s_last_touch_us = 0;
static bool s_dimmed = false;
static bool s_update_req = false;
static bool s_ui_ready = false;
static esp_timer_handle_t s_periodic_timer = NULL;

static void backlight_set_full(void)
{
    (void)bsp_display_brightness_set(100);
    (void)bsp_display_backlight_on();
    s_dimmed = false;
}

static void backlight_check_dim(void)
{
    const uint32_t timeout_s = CONFIG_BMU_DISPLAY_BL_DIM_TIMEOUT_S;
    if (timeout_s == 0) {
        return;
    }

    const int64_t now = esp_timer_get_time();
    const int64_t elapsed_us = now - s_last_touch_us;

    if (!s_dimmed && elapsed_us > (int64_t)timeout_s * 1000000LL) {
        (void)bsp_display_brightness_set(12);
        s_dimmed = true;
        ESP_LOGI(TAG, "Backlight attenue (inactivite %ds)", (int)timeout_s);
    }
}

static void display_periodic_cb(void *arg)
{
    (void)arg;
    backlight_check_dim();

    if (s_disp == NULL || !s_ui_ready) {
        return;
    }

    if (bsp_display_lock(0)) {
        bmu_ui_main_update(&s_ui_ctx);
        if (s_update_req) {
            s_update_req = false;
            lv_obj_invalidate(lv_scr_act());
        }
        bsp_display_unlock();
    }
}

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

    ESP_LOGI(TAG, "=== Initialisation affichage BMU via BSP BOX-3 ===");

    s_disp = bsp_display_start();
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start a echoue");
        return ESP_FAIL;
    }

    if (!bsp_display_lock(0)) {
        ESP_LOGE(TAG, "Impossible de prendre le lock LVGL");
        return ESP_FAIL;
    }

    // LVGL v9: default theme is set automatically by lv_display_create.
    // Set dark mode via style on the screen object instead:
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x1E1E1E), 0);

    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, BSP_LCD_H_RES, BSP_LCD_V_RES);
    lv_scr_load(s_screen);

    bmu_ui_main_create(s_screen, &s_ui_ctx);
    s_ui_ready = true;
    bsp_display_unlock();

    s_last_touch_us = esp_timer_get_time();
    backlight_set_full();

    lv_indev_t *indev = bsp_display_get_input_dev();
    if (indev != NULL) {
        lv_indev_add_event_cb(
            indev,
            [](lv_event_t *e) {
                (void)e;
                bmu_display_wake();
            },
            LV_EVENT_PRESSING,
            NULL);
    }

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

    ESP_LOGI(TAG, "=== Affichage BMU pret (refresh=%dms, dim=%ds) ===",
             CONFIG_BMU_DISPLAY_REFRESH_MS,
             CONFIG_BMU_DISPLAY_BL_DIM_TIMEOUT_S);
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
    return s_screen;
}
