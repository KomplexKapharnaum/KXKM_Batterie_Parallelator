// firmware-idf-v2/main/main.cpp
//
// Boot Phase 12 : init NVS, init BSP BOX-3, init LVGL port, affiche un
// splash, puis initialise le core Rust via `bmu_core_init` et tourne un
// fake tick loop 1 Hz qui feed un `BmuRawInputs` vide.

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "bsp/esp-bsp.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

extern "C" {
#include "bmu_core.h"
#include "bmu_i2c_glue.h"
#include "bmu_climate.h"
#include "bmu_sd_log.h"
}
#include "bmu_soh.h"

#include "task_bmu_core.h"

static const char *TAG = "bmu-v2";

static BmuCore *s_core = nullptr;

static void init_nvs(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS erase required");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
}

static void init_display_and_splash(void) {
    // BSP init LCD + LVGL port
    lv_display_t *disp = bsp_display_start();
    if (disp == NULL) {
        ESP_LOGE(TAG, "bsp_display_start FAILED");
        return;
    }
    bsp_display_backlight_on();

    // Lock LVGL, build splash screen, unlock
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock FAILED");
        return;
    }

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0a0a0a), LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "BMU v2");
    lv_obj_set_style_text_color(title, lv_color_hex(0x00ff88), LV_PART_MAIN);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "Rust Hybrid Core -- phase 12");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xcccccc), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 20);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "Splash displayed");
}

static void init_bmu_core(void) {
    BmuConfigC cfg = {};
    cfg.umin_mv             = 24000;
    cfg.umax_mv             = 30000;
    cfg.imax_ma             = 1000;
    cfg.vdiff_imbalance_mv  = 1000;
    cfg.nb_switch_max       = 5;
    cfg.reconnect_delay_ms  = 10000;
    cfg.tick_period_ms      = 200;

    s_core = bmu_core_init(&cfg);
    if (s_core == nullptr) {
        ESP_LOGE(TAG, "bmu_core_init returned NULL -- check cfg validation");
        return;
    }
    ESP_LOGI(TAG, "bmu_core_init OK, handle=%p", (void *)s_core);
}

static void init_i2c_glue(void) {
    esp_err_t err = bmu_i2c_glue_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmu_i2c_glue_init failed: %s", esp_err_to_name(err));
        return;
    }

    uint8_t n_ina = 0, n_tca = 0;
    err = bmu_i2c_glue_scan(&n_ina, &n_tca);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmu_i2c_glue_scan failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "I2C scan: INA=%u TCA=%u", n_ina, n_tca);

    // Programme SHUNT_CAL sur tous les INA237 detectes (Phase 14 Task 14.2)
    bmu_i2c_glue_program_shunt_cal();
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== BMU v2 boot ===");
    ESP_LOGI(TAG, "FW version: %s", CONFIG_APP_PROJECT_VER);
    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());

    init_nvs();
    init_display_and_splash();
    init_bmu_core();

    if (s_core == nullptr) {
        ESP_LOGE(TAG, "Core init failed, halting");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    init_i2c_glue();

    // Phase 15 -- aggregator climate (no FreeRTOS task, lazy stats)
    ESP_LOGI(TAG, "init bmu_climate (heap=%u kB)",
             (unsigned)(esp_get_free_heap_size() / 1024));
    bmu_climate_init();

    // Phase 15 -- TFLite Micro SOH
    ESP_LOGI(TAG, "init bmu_soh (heap=%u kB)",
             (unsigned)(esp_get_free_heap_size() / 1024));
    esp_err_t soh_err = bmu_soh_init();
    if (soh_err != ESP_OK) {
        ESP_LOGW(TAG, "bmu_soh_init failed: %s -- task_soh will be no-op",
                 esp_err_to_name(soh_err));
    }

    // Phase 15 -- SD log writer (FAT partition fatfs, NOSYNC files)
    ESP_LOGI(TAG, "init bmu_sd_log (heap=%u kB)",
             (unsigned)(esp_get_free_heap_size() / 1024));
    esp_err_t sd_err = bmu_sd_log_init();
    if (sd_err != ESP_OK) {
        ESP_LOGW(TAG, "bmu_sd_log_init failed: %s -- task_sd_log will be no-op",
                 esp_err_to_name(sd_err));
    }

    ESP_LOGI(TAG, "Boot complete -- starting task_bmu_core (PRO_CPU, 5 Hz)");
    task_bmu_core_start(s_core);

    // Phase 15 -- secondary tasks on APP_CPU
    task_soh_start(s_core);
    task_sd_log_start(s_core);

    // app_main reste en idle et log la heap toutes les 10 s
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "main idle: heap_free=%u kB",
                 (unsigned)(esp_get_free_heap_size() / 1024));
    }
}
