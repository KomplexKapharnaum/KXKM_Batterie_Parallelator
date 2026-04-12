#pragma once

#include "bmu_ina237.h"
#include "bmu_config.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bmu_ina237_t       *ina_devices;
    uint8_t             nb_ina;
    SemaphoreHandle_t   mutex;
    float               last_voltage_mv[BMU_MAX_BATTERIES];
    float               last_current_a[BMU_MAX_BATTERIES];
    float               ah_discharge[BMU_MAX_BATTERIES];
    float               ah_charge[BMU_MAX_BATTERIES];
    TaskHandle_t        ah_task_handle;
    bool                ah_running;
} bmu_battery_manager_t;

esp_err_t bmu_battery_manager_init(bmu_battery_manager_t *mgr,
                                    bmu_ina237_t *ina, uint8_t nb_ina);
esp_err_t bmu_battery_manager_start(bmu_battery_manager_t *mgr);
esp_err_t bmu_battery_manager_get_summary(bmu_battery_manager_t *mgr,
                                          float *avg_voltage_mv,
                                          float *total_current_a,
                                          uint8_t *sample_count);
float bmu_battery_manager_get_max_voltage_mv(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_min_voltage_mv(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_avg_voltage_mv(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_ah_discharge(bmu_battery_manager_t *mgr, int idx);
float bmu_battery_manager_get_ah_charge(bmu_battery_manager_t *mgr, int idx);
float bmu_battery_manager_get_total_current_a(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_last_voltage_mv(bmu_battery_manager_t *mgr, int idx);
float bmu_battery_manager_get_last_current_a(bmu_battery_manager_t *mgr, int idx);

/**
 * @brief Update nb_ina after hotplug topology change.
 */
esp_err_t bmu_battery_manager_update_nb_ina(bmu_battery_manager_t *mgr, uint8_t new_nb_ina);

#ifdef __cplusplus
}
#endif
