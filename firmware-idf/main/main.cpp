/**
 * @file main.cpp
 * @brief Point d'entrée BMU ESP-IDF — init I2C, scan INA237/TCA9535, boucle protection.
 *
 * Phase 0+1 : scaffold + drivers I2C.
 * La logique de protection (Phase 2) viendra dans bmu_protection.
 */
#include "bmu_i2c.h"
#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>

static const char *TAG = "MAIN";

/* ── Config hardware (sera migré dans bmu_config en Phase 2) ────────────── */
static const uint32_t SHUNT_UOHM      = 2000;     // 2 mΩ = 2000 µΩ (PCB BMU v2)
static const float    MAX_CURRENT_A    = 10.0f;    // Plage INA237
static const uint16_t ALERT_OV_MV     = 30000;     // Sur-tension mV
static const uint16_t ALERT_UV_MV     = 24000;     // Sous-tension mV

#define INA_MAX  16
#define TCA_MAX   8

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "══════════════════════════════════════════════");
    ESP_LOGI(TAG, "  KXKM BMU — ESP-IDF v5.3 (Phase 0+1)");
    ESP_LOGI(TAG, "  Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGI(TAG, "══════════════════════════════════════════════");

    /* ── Init bus I2C dédié BMU sur PMOD1 (GPIO40/41, 50kHz) ───────────── */
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(bmu_i2c_init(&i2c_bus));

    /* ── Scan diagnostique ─────────────────────────────────────────────── */
    int total_devices = bmu_i2c_scan(i2c_bus);
    ESP_LOGI(TAG, "I2C scan: %d device(s) total", total_devices);

    /* ── Init INA237 (capteurs V/I) ────────────────────────────────────── */
    bmu_ina237_t ina[INA_MAX] = {};
    uint8_t nb_ina = 0;
    esp_err_t ret = bmu_ina237_scan_init(i2c_bus, SHUNT_UOHM, MAX_CURRENT_A,
                                          ina, &nb_ina);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "INA237 scan failed: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "INA237: %d capteur(s) initialisé(s)", nb_ina);

    /* ── Configurer alertes tension sur chaque INA237 ──────────────────── */
    for (int i = 0; i < nb_ina; i++) {
        bmu_ina237_set_bus_voltage_alerts(&ina[i], ALERT_OV_MV, ALERT_UV_MV);
    }

    /* ── Init TCA9535 (switches + LEDs) ────────────────────────────────── */
    bmu_tca9535_handle_t tca[TCA_MAX] = {};
    uint8_t nb_tca = 0;
    ret = bmu_tca9535_scan_init(i2c_bus, tca, TCA_MAX, &nb_tca);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA9535 scan failed: %s", esp_err_to_name(ret));
    }
    ESP_LOGI(TAG, "TCA9535: %d expandeur(s) initialisé(s)", nb_tca);

    /* ── Validation topologie : Nb_TCA * 4 == Nb_INA ──────────────────── */
    bool topology_valid = (nb_ina > 0) && (nb_tca > 0) && (nb_tca * 4 == nb_ina);
    if (!topology_valid) {
        ESP_LOGE(TAG, "TOPOLOGIE INVALIDE: Nb_TCA=%d * 4 = %d != Nb_INA=%d",
                 nb_tca, nb_tca * 4, nb_ina);
        ESP_LOGE(TAG, "FAIL-SAFE: toutes les batteries seront forcées OFF");
    } else {
        ESP_LOGI(TAG, "Topologie OK: %d TCA × 4 = %d INA", nb_tca, nb_ina);
    }

    /* ── Boucle principale (500 ms) ────────────────────────────────────── */
    while (true) {
        if (!topology_valid) {
            /* Fail-safe : tout couper */
            for (int t = 0; t < nb_tca; t++) {
                bmu_tca9535_all_off(&tca[t]);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        /* Lecture de toutes les batteries */
        for (int i = 0; i < nb_ina; i++) {
            float v_mv = 0, i_a = 0;
            esp_err_t r = bmu_ina237_read_voltage_current(&ina[i], &v_mv, &i_a);
            if (r == ESP_OK && !isnan(v_mv)) {
                ESP_LOGI(TAG, "BAT[%02d] V=%.0fmV I=%.3fA", i + 1, v_mv, i_a);

                /* Phase 0+1 : LED vert/rouge selon tension */
                int tca_idx = i / 4;
                int channel = i % 4;
                if (tca_idx < nb_tca) {
                    bool ok = (v_mv >= ALERT_UV_MV && v_mv <= ALERT_OV_MV);
                    bmu_tca9535_set_led(&tca[tca_idx], channel, !ok, ok);
                }
            } else {
                ESP_LOGW(TAG, "BAT[%02d] lecture erreur: %s", i + 1, esp_err_to_name(r));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
