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
    float               ah_discharge[BMU_MAX_BATTERIES];
    float               ah_charge[BMU_MAX_BATTERIES];
    TaskHandle_t        ah_tasks[BMU_MAX_BATTERIES];
    bool                ah_running[BMU_MAX_BATTERIES];
} bmu_battery_manager_t;

esp_err_t bmu_battery_manager_init(bmu_battery_manager_t *mgr,
                                    bmu_ina237_t *ina, uint8_t nb_ina);
esp_err_t bmu_battery_manager_start_ah_task(bmu_battery_manager_t *mgr, int idx);
float bmu_battery_manager_get_max_voltage_mv(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_min_voltage_mv(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_avg_voltage_mv(bmu_battery_manager_t *mgr);
float bmu_battery_manager_get_ah_discharge(bmu_battery_manager_t *mgr, int idx);
float bmu_battery_manager_get_ah_charge(bmu_battery_manager_t *mgr, int idx);
float bmu_battery_manager_get_total_current_a(bmu_battery_manager_t *mgr);

#ifdef __cplusplus
}
#endif
