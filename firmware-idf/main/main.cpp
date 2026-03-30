/**
 * @file main.cpp
 * @brief KXKM BMU — ESP-IDF v5.3 (Phase 3: WiFi + Web + Storage)
 */
#include "bmu_i2c.h"
#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_config.h"
#include "bmu_wifi.h"
#include "bmu_web.h"
#include "bmu_storage.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

static esp_err_t init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_OK) {
        size_t total = 0, used = 0;
        esp_spiffs_info("storage", &total, &used);
        ESP_LOGI(TAG, "SPIFFS: total=%d used=%d", total, used);
    } else {
        ESP_LOGW(TAG, "SPIFFS mount failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "══════════════════════════════════════════════");
    ESP_LOGI(TAG, "  KXKM BMU — ESP-IDF v5.3 (Phase 3)");
    ESP_LOGI(TAG, "══════════════════════════════════════════════");
    bmu_config_log();

    /* ── 1. NVS + WiFi ─────────────────────────────────────────────── */
    bmu_nvs_init();
    bmu_wifi_init();

    /* ── 2. SPIFFS (web assets) ────────────────────────────────────── */
    init_spiffs();

    /* ── 3. SD card ────────────────────────────────────────────────── */
    bmu_sd_init();

    /* ── 4. I2C + sensors (unchanged from Phase 2) ─────────────────── */
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(bmu_i2c_init(&i2c_bus));
    bmu_i2c_scan(i2c_bus);

    static bmu_ina237_t ina[BMU_MAX_BATTERIES] = {};
    uint8_t nb_ina = 0;
    bmu_ina237_scan_init(i2c_bus, 2000, 10.0f, ina, &nb_ina);
    for (int i = 0; i < nb_ina; i++) {
        bmu_ina237_set_bus_voltage_alerts(&ina[i], BMU_MAX_VOLTAGE_MV, BMU_MIN_VOLTAGE_MV);
    }

    static bmu_tca9535_handle_t tca[BMU_MAX_TCA] = {};
    uint8_t nb_tca = 0;
    bmu_tca9535_scan_init(i2c_bus, tca, BMU_MAX_TCA, &nb_tca);

    bool topology_ok = (nb_ina > 0) && (nb_tca > 0) && (nb_tca * 4 == nb_ina);
    if (!topology_ok) {
        ESP_LOGE(TAG, "TOPOLOGY MISMATCH %d TCA * 4 != %d INA — FAIL-SAFE", nb_tca, nb_ina);
    }

    /* ── 5. Protection + Battery Manager ───────────────────────────── */
    static bmu_protection_ctx_t prot = {};
    ESP_ERROR_CHECK(bmu_protection_init(&prot, ina, nb_ina, tca, nb_tca));

    static bmu_battery_manager_t mgr = {};
    bmu_battery_manager_init(&mgr, ina, nb_ina);
    for (int i = 0; i < nb_ina; i++) {
        bmu_battery_manager_start_ah_task(&mgr, i);
    }

    /* ── 6. Web server (needs prot + mgr handles) ──────────────────── */
    if (bmu_wifi_is_connected()) {
        static bmu_web_ctx_t web_ctx = { .prot = &prot, .mgr = &mgr };
        bmu_web_start(&web_ctx);
        char ip[16] = {};
        bmu_wifi_get_ip(ip, sizeof(ip));
        ESP_LOGI(TAG, "Web server started at http://%s/", ip);
    } else {
        ESP_LOGW(TAG, "WiFi not connected — web server not started");
    }

    ESP_LOGI(TAG, "Init complete — protection loop (%d ms)", BMU_LOOP_PERIOD_MS);

    /* ── Main protection loop ──────────────────────────────────────── */
    while (true) {
        if (!topology_ok) {
            bmu_protection_all_off(&prot);
        } else {
            for (int i = 0; i < nb_ina; i++) {
                bmu_protection_check_battery(&prot, i);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BMU_LOOP_PERIOD_MS));
    }
}
