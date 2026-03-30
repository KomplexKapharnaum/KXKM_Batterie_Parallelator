#include "bmu_i2c.h"
#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "══════════════════════════════════════════════");
    ESP_LOGI(TAG, "  KXKM BMU — ESP-IDF v5.3 (Phase 2)");
    ESP_LOGI(TAG, "══════════════════════════════════════════════");
    bmu_config_log();

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

    static bmu_protection_ctx_t prot = {};
    ESP_ERROR_CHECK(bmu_protection_init(&prot, ina, nb_ina, tca, nb_tca));

    static bmu_battery_manager_t mgr = {};
    bmu_battery_manager_init(&mgr, ina, nb_ina);
    for (int i = 0; i < nb_ina; i++) {
        bmu_battery_manager_start_ah_task(&mgr, i);
    }

    ESP_LOGI(TAG, "Init complete — protection loop (%d ms)", BMU_LOOP_PERIOD_MS);

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
