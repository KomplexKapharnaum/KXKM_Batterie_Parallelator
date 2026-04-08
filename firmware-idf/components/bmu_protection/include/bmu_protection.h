#pragma once

#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "bmu_config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BMU_STATE_CONNECTED,
    BMU_STATE_DISCONNECTED,
    BMU_STATE_RECONNECTING,
    BMU_STATE_ERROR,
    BMU_STATE_LOCKED
} bmu_battery_state_t;

typedef struct {
    bmu_ina237_t         *ina_devices;
    bmu_tca9535_handle_t *tca_devices;
    uint8_t               nb_ina;
    uint8_t               nb_tca;

    SemaphoreHandle_t     state_mutex;
    float                 battery_voltages[BMU_MAX_BATTERIES];
    int                   nb_switch[BMU_MAX_BATTERIES];
    int64_t               reconnect_time_ms[BMU_MAX_BATTERIES];
    bmu_battery_state_t   battery_state[BMU_MAX_BATTERIES];
} bmu_protection_ctx_t;

esp_err_t bmu_protection_init(bmu_protection_ctx_t *ctx,
                               bmu_ina237_t *ina, uint8_t nb_ina,
                               bmu_tca9535_handle_t *tca, uint8_t nb_tca);

esp_err_t bmu_protection_check_battery(bmu_protection_ctx_t *ctx, int battery_idx);

esp_err_t bmu_protection_all_off(bmu_protection_ctx_t *ctx);

esp_err_t bmu_protection_reset_switch_count(bmu_protection_ctx_t *ctx, int battery_idx);

bmu_battery_state_t bmu_protection_get_state(bmu_protection_ctx_t *ctx, int battery_idx);

float bmu_protection_get_voltage(bmu_protection_ctx_t *ctx, int battery_idx);
esp_err_t bmu_protection_get_switch_count(bmu_protection_ctx_t *ctx, int battery_idx,
                                          int *switch_count_out);

/**
 * @brief Web-initiated switch command. Validates voltage precondition before switching.
 * Respects locked batteries — returns ESP_ERR_NOT_ALLOWED for locked batteries.
 */
esp_err_t bmu_protection_web_switch(bmu_protection_ctx_t *ctx, int battery_idx, bool on);

/**
 * @brief Update topology counts after hotplug re-scan.
 * Resets state of new battery slots to DISCONNECTED.
 * Must be called with valid nb_ina <= BMU_MAX_BATTERIES and nb_tca <= BMU_MAX_TCA.
 */
esp_err_t bmu_protection_update_topology(bmu_protection_ctx_t *ctx,
                                          uint8_t new_nb_ina,
                                          uint8_t new_nb_tca);

#ifdef __cplusplus
}
#endif
