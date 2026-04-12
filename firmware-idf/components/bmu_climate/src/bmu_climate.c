// firmware-idf-v2/components/bmu_climate/src/bmu_climate.c
//
// Phase 15 -- agregateur stats AHT30. Voir bmu_climate.h pour le contrat.
//
// Implementation : ring buffer 300 samples (1 min @ 5 Hz). Stats calculees
// au moment de `bmu_climate_get_stats()` (lazy). Pas de mutex : on suppose
// 1 producteur (task_bmu_core) et 1 consommateur (LVGL/MQTT, hors phase 15).
// Tearing acceptable -- les stats se mettent a jour a chaque tick.

#include "bmu_climate.h"

#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "climate";

#define BMU_CLIMATE_WINDOW 300  // 1 min @ 5 Hz

typedef struct {
    int16_t  temp_c10[BMU_CLIMATE_WINDOW];
    uint16_t rh_pct10[BMU_CLIMATE_WINDOW];
    uint32_t n;        // nb samples effectifs (plafonne a WINDOW)
    uint32_t idx;      // prochain index d'insertion
    uint32_t total_n;  // total cumule pour debug
} bmu_climate_state_t;

static bmu_climate_state_t s_state;

esp_err_t bmu_climate_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    ESP_LOGI(TAG, "bmu_climate_init OK (window=%u samples = 60 s @ 5 Hz)",
             (unsigned)BMU_CLIMATE_WINDOW);
    return ESP_OK;
}

void bmu_climate_sample(int16_t temp_c10, uint16_t rh_pct10)
{
    // Skip samples vides (capteur non present sur le bench Phase 14/15)
    if (temp_c10 == 0 && rh_pct10 == 0) {
        return;
    }

    s_state.temp_c10[s_state.idx] = temp_c10;
    s_state.rh_pct10[s_state.idx] = rh_pct10;
    s_state.idx = (s_state.idx + 1) % BMU_CLIMATE_WINDOW;
    if (s_state.n < BMU_CLIMATE_WINDOW) {
        s_state.n++;
    }
    s_state.total_n++;
}

esp_err_t bmu_climate_get_stats(bmu_climate_stats_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    uint32_t n = s_state.n;
    if (n == 0) {
        out->valid = false;
        return ESP_ERR_INVALID_STATE;
    }

    int32_t  t_sum  = 0;
    uint32_t rh_sum = 0;
    int16_t  t_min  = INT16_MAX;
    int16_t  t_max  = INT16_MIN;
    uint16_t rh_min = UINT16_MAX;
    uint16_t rh_max = 0;

    for (uint32_t i = 0; i < n; i++) {
        int16_t  t  = s_state.temp_c10[i];
        uint16_t rh = s_state.rh_pct10[i];
        t_sum  += t;
        rh_sum += rh;
        if (t  < t_min ) t_min  = t;
        if (t  > t_max ) t_max  = t;
        if (rh < rh_min) rh_min = rh;
        if (rh > rh_max) rh_max = rh;
    }

    out->valid        = true;
    out->n_samples    = n;
    out->temp_c10_min = t_min;
    out->temp_c10_max = t_max;
    out->temp_c10_avg = (int16_t)(t_sum / (int32_t)n);
    out->rh_pct10_min = rh_min;
    out->rh_pct10_max = rh_max;
    out->rh_pct10_avg = (uint16_t)(rh_sum / n);
    return ESP_OK;
}
