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
}

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

// Statique : `BmuSnapshotC` fait plus de 600 bytes (16 batteries) ce qui
// depasse le stack par defaut de main_task. On garde un seul exemplaire
// en .bss pour la fake loop Phase 12.
static BmuRawInputs s_raw;
static BmuSnapshotC s_snap;
static BmuActionsC s_actions;

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
}

static void tick_loop_real(void) {
    memset(&s_raw, 0, sizeof(s_raw));
    memset(&s_snap, 0, sizeof(s_snap));
    memset(&s_actions, 0, sizeof(s_actions));

    while (true) {
        esp_err_t err = bmu_i2c_glue_read_inputs(&s_raw);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "read_inputs failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        int32_t rc = bmu_core_tick(s_core, &s_raw, &s_snap, &s_actions);
        if (rc != 0) {
            ESP_LOGW(TAG, "bmu_core_tick rc=%ld n_bat=%u topo=%u",
                     (long)rc,
                     (unsigned)s_snap.n_bat,
                     (unsigned)s_snap.system.topology_ok);
        } else if (s_snap.n_bat > 0) {
            ESP_LOGI(TAG,
                     "tick OK n_bat=%u topo=%u bat0: V=%ldmv I=%ldma state=%u "
                     "tca0=0x%02X climate T=%d rh=%u heap=%u kB",
                     (unsigned)s_snap.n_bat,
                     (unsigned)s_snap.system.topology_ok,
                     (long)s_snap.batteries[0].voltage_mv,
                     (long)s_snap.batteries[0].current_ma,
                     (unsigned)s_snap.batteries[0].state,
                     (unsigned)s_raw.tca_inputs[0],
                     (int)s_raw.climate_temp_c10,
                     (unsigned)s_raw.climate_rh_pct10,
                     (unsigned)(esp_get_free_heap_size() / 1024));
        } else {
            ESP_LOGI(TAG,
                     "tick OK n_bat=0 topo=%u climate T=%d rh=%u heap=%u kB",
                     (unsigned)s_snap.system.topology_ok,
                     (int)s_raw.climate_temp_c10,
                     (unsigned)s_raw.climate_rh_pct10,
                     (unsigned)(esp_get_free_heap_size() / 1024));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
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

    ESP_LOGI(TAG, "Boot complete -- entering real tick loop 1Hz");
    tick_loop_real();
}
