/**
 * @file bmu_rint_output.cpp
 * @brief Stub de routage de sortie — sera implémenté en Task 4.
 *
 * Ce fichier est un placeholder pour permettre la compilation du composant
 * bmu_rint avant l'implémentation complète du routage (MQTT, InfluxDB, display).
 */

#include "bmu_rint.h"
#include "esp_log.h"

static const char *TAG = "RINT_OUT";

extern "C" void rint_output_route(uint8_t idx, bmu_rint_trigger_t trigger,
                                  const bmu_rint_result_t *res)
{
    if (res == NULL) return;

    ESP_LOGI(TAG, "Bat %d [trigger=%d] R_ohmic=%.1f mΩ R_total=%.1f mΩ valid=%d",
             idx, (int)trigger,
             res->r_ohmic_mohm, res->r_total_mohm, (int)res->valid);
}
