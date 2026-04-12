#pragma once

#include "esp_err.h"
#include "bmu_battery_manager.h"
#include "bmu_protection.h"
#include "bmu_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize TFLite Micro interpreter for SOH prediction.
 * Call once after bmu_battery_manager_start().
 */
esp_err_t bmu_soh_init(void);

/**
 * @brief Run SOH inference for one battery channel.
 *
 * Reads V, I, Ah from battery_manager/protection, builds the 13-feature
 * vector, normalises, runs TFLite INT8 inference.
 *
 * @param mgr   Battery manager (for Ah, INA devices)
 * @param prot  Protection context (for cached voltages)
 * @param idx   Battery channel index (0..nb_ina-1)
 * @return SOH value 0.0-1.0, or -1.0 on error
 */
float bmu_soh_predict(bmu_battery_manager_t *mgr,
                      bmu_protection_ctx_t *prot,
                      int idx);

/**
 * @brief Run SOH inference for all batteries, store in internal cache.
 * Called periodically (e.g. every 10s) from main loop or timer.
 */
void bmu_soh_update_all(bmu_battery_manager_t *mgr,
                        bmu_protection_ctx_t *prot,
                        int nb_ina);

/**
 * @brief Get cached SOH value for a battery (from last update_all).
 * @return SOH 0.0-1.0, or -1.0 if not yet computed
 */
float bmu_soh_get_cached(int idx);

#ifdef __cplusplus
}
#endif
