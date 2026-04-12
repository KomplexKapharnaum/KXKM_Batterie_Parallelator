// firmware-idf-v2/components/bmu_climate/include/bmu_climate.h
//
// Phase 15 -- agregateur stats AHT30 (min/max/avg sur fenetre 1 min @ 5 Hz).
//
// Le capteur AHT30 est lu par `bmu_i2c_glue` (Phase 13). Ce composant ne
// touche jamais a I2C : il est alimente par `bmu_climate_sample()` appele
// depuis `task_bmu_core` a chaque tick avec les valeurs deja decodees du
// `BmuRawInputs` (`climate_temp_c10`, `climate_rh_pct10`).
//
// Pas de tache FreeRTOS dediee : agregation pure-CPU, ~1 us / sample.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     valid;        // true des qu'au moins 1 sample non-nul recu
    uint32_t n_samples;    // nb samples dans la fenetre courante
    int16_t  temp_c10_min; // min temperature (1/10 degC)
    int16_t  temp_c10_max;
    int16_t  temp_c10_avg;
    uint16_t rh_pct10_min; // min humidite (1/10 %RH)
    uint16_t rh_pct10_max;
    uint16_t rh_pct10_avg;
} bmu_climate_stats_t;

// Init -- reset window. Idempotent.
esp_err_t bmu_climate_init(void);

// Push un sample (appel sync depuis task_bmu_core, 5 Hz).
// Les valeurs zero sont ignorees (capteur non present sur le bench).
void bmu_climate_sample(int16_t temp_c10, uint16_t rh_pct10);

// Snapshot des stats actuelles. Retourne ESP_ERR_INVALID_STATE si jamais
// rien recu.
esp_err_t bmu_climate_get_stats(bmu_climate_stats_t *out);

#ifdef __cplusplus
}
#endif
