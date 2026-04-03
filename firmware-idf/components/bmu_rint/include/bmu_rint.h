#pragma once

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#include "bmu_protection.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BMU_RINT_TRIGGER_OPPORTUNISTIC,
    BMU_RINT_TRIGGER_PERIODIC,
    BMU_RINT_TRIGGER_ON_DEMAND,
} bmu_rint_trigger_t;

typedef struct {
    float   r_ohmic_mohm;       // R₀ ohmique (mΩ) — at +100 ms
    float   r_total_mohm;       // R₀ + R₁ total (mΩ) — at +1 s
    float   v_load_mv;          // V₁ under load (mV)
    float   v_ocv_fast_mv;      // V₂ at +PULSE_FAST_MS (mV)
    float   v_ocv_stable_mv;    // V₃ at +PULSE_TOTAL_MS (mV)
    float   i_load_a;           // I₁ at measurement start (A)
    int64_t timestamp_ms;       // Measurement time (epoch ms or uptime ms)
    bool    valid;              // Measurement passed all checks
} bmu_rint_result_t;

/**
 * Injecter le contexte protection (appeler avant init).
 */
void bmu_rint_set_ctx(bmu_protection_ctx_t *ctx);

esp_err_t bmu_rint_init(void);
esp_err_t bmu_rint_measure(uint8_t battery_idx, bmu_rint_trigger_t trigger);
esp_err_t bmu_rint_measure_all(bmu_rint_trigger_t trigger);
bmu_rint_result_t bmu_rint_get_cached(uint8_t battery_idx);
void bmu_rint_on_disconnect(uint8_t battery_idx, float v_before_mv, float i_before_a);
esp_err_t bmu_rint_start_periodic(void);

#ifdef __cplusplus
}
#endif
