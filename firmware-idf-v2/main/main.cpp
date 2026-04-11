// firmware-idf-v2/main/main.cpp
//
// Boot minimal Phase 11 : init NVS, init BSP BOX-3, init LVGL port, affiche
// un splash "BMU v2 -- Rust Hybrid core" pendant le boot.
//
// Aucun appel au core Rust a ce stade (Phase 12).

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

static const char *TAG = "bmu-v2";

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
    lv_label_set_text(subtitle, "Rust Hybrid Core -- phase 11");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0xcccccc), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 20);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "Splash displayed");
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== BMU v2 boot ===");
    ESP_LOGI(TAG, "FW version: %s", CONFIG_APP_PROJECT_VER);
    ESP_LOGI(TAG, "ESP-IDF: %s", esp_get_idf_version());

    init_nvs();
    init_display_and_splash();

    ESP_LOGI(TAG, "Boot complete -- entering idle loop");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "heap free=%lu kB uptime=%llu s",
                 (unsigned long)(esp_get_free_heap_size() / 1024),
                 (unsigned long long)(esp_timer_get_time() / 1000000ULL));
    }
}
