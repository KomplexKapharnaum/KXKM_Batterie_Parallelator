#include "bmu_battery_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>
#include <cstring>
#include <cstdlib>

static const char *TAG = "BMGR";

esp_err_t bmu_battery_manager_init(bmu_battery_manager_t *mgr,
                                    bmu_ina237_t *ina, uint8_t nb_ina)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->ina_devices = ina;
    mgr->nb_ina = nb_ina;
    mgr->mutex = xSemaphoreCreateMutex();
    configASSERT(mgr->mutex != NULL);
    ESP_LOGI(TAG, "Battery manager init: %d batteries", nb_ina);
    return ESP_OK;
}

static void ah_task(void *pv)
{
    bmu_battery_manager_t *mgr = (bmu_battery_manager_t *)pv;
    int64_t last_us = esp_timer_get_time();
    int sample = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t now_us = esp_timer_get_time();
        float elapsed_h = (float)(now_us - last_us) / 3.6e9f;
        last_us = now_us;

        for (int i = 0; i < mgr->nb_ina; i++) {
            float voltage_mv = 0.0f;
            float current_a = 0;
            if (bmu_ina237_read_voltage_current(&mgr->ina_devices[i], &voltage_mv, &current_a) != ESP_OK
                || std::isnan(voltage_mv) || std::isnan(current_a))
                continue;

            if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                mgr->last_voltage_mv[i] = voltage_mv;
                mgr->last_current_a[i] = current_a;
                if (elapsed_h > 0) {
                    if (current_a > 0) {
                        mgr->ah_discharge[i] += current_a * elapsed_h;
                    } else if (current_a < 0) {
                        mgr->ah_charge[i] += (-current_a) * elapsed_h;
                    }
                }
                xSemaphoreGive(mgr->mutex);
            }
        }

        /* Periodic log every ~100s */
        if (++sample % 100 == 0) {
            for (int i = 0; i < mgr->nb_ina; i++) {
                ESP_LOGI(TAG, "BAT[%d] Ah discharge=%.3f charge=%.3f",
                         i + 1, mgr->ah_discharge[i], mgr->ah_charge[i]);
            }
        }
    }
}

esp_err_t bmu_battery_manager_start(bmu_battery_manager_t *mgr)
{
    if (mgr == NULL) return ESP_ERR_INVALID_ARG;

    if (mgr->ah_running) return ESP_OK;

    TaskHandle_t handle = NULL;
    if (xTaskCreate(ah_task, "Ah_all", 4096, mgr, 1, &handle) != pdPASS) {
        return ESP_FAIL;
    }

    mgr->ah_task_handle = handle;
    mgr->ah_running = true;
    ESP_LOGI(TAG, "Ah task started (single task, %d batteries)", mgr->nb_ina);
    return ESP_OK;
}

esp_err_t bmu_battery_manager_get_summary(bmu_battery_manager_t *mgr,
                                          float *avg_voltage_mv,
                                          float *total_current_a,
                                          uint8_t *sample_count)
{
    if (mgr == NULL || avg_voltage_mv == NULL || total_current_a == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float sum_voltage_mv = 0.0f;
    float sum_current_a = 0.0f;
    uint8_t valid_samples = 0;

    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (int i = 0; i < mgr->nb_ina; i++) {
        const float voltage_mv = mgr->last_voltage_mv[i];
        const float current_a = mgr->last_current_a[i];
        if (std::isnan(voltage_mv) || std::isnan(current_a) || voltage_mv <= 1000.0f) {
            continue;
        }

        sum_voltage_mv += voltage_mv;
        sum_current_a += current_a;
        valid_samples++;
    }

    xSemaphoreGive(mgr->mutex);

    *avg_voltage_mv = (valid_samples > 0) ? (sum_voltage_mv / valid_samples) : 0.0f;
    *total_current_a = sum_current_a;
    if (sample_count != NULL) {
        *sample_count = valid_samples;
    }

    return (valid_samples > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

float bmu_battery_manager_get_max_voltage_mv(bmu_battery_manager_t *mgr)
{
    float max_mv = 0;
    if (mgr == NULL) return 0;

    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        for (int i = 0; i < mgr->nb_ina; i++) {
            const float v = mgr->last_voltage_mv[i];
            if (!std::isnan(v) && v > max_mv) {
                max_mv = v;
            }
        }
        xSemaphoreGive(mgr->mutex);
    }
    return max_mv;
}

float bmu_battery_manager_get_min_voltage_mv(bmu_battery_manager_t *mgr)
{
    float min_mv = 999999;
    if (mgr == NULL) return 0;

    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        for (int i = 0; i < mgr->nb_ina; i++) {
            const float v = mgr->last_voltage_mv[i];
            if (!std::isnan(v) && v > 1000.0f && v < min_mv) {
                min_mv = v;
            }
        }
        xSemaphoreGive(mgr->mutex);
    }
    return min_mv < 999999 ? min_mv : 0;
}

float bmu_battery_manager_get_avg_voltage_mv(bmu_battery_manager_t *mgr)
{
    float avg_mv = 0.0f;
    float total_current_a = 0.0f;
    (void)bmu_battery_manager_get_summary(mgr, &avg_mv, &total_current_a, NULL);
    return avg_mv;
}

float bmu_battery_manager_get_ah_discharge(bmu_battery_manager_t *mgr, int idx)
{
    if (idx < 0 || idx >= BMU_MAX_BATTERIES) return 0;
    float v = 0;
    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        v = mgr->ah_discharge[idx];
        xSemaphoreGive(mgr->mutex);
    }
    return v;
}

float bmu_battery_manager_get_ah_charge(bmu_battery_manager_t *mgr, int idx)
{
    if (idx < 0 || idx >= BMU_MAX_BATTERIES) return 0;
    float v = 0;
    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        v = mgr->ah_charge[idx];
        xSemaphoreGive(mgr->mutex);
    }
    return v;
}

float bmu_battery_manager_get_total_current_a(bmu_battery_manager_t *mgr)
{
    float avg_mv = 0.0f;
    float total_current_a = 0.0f;
    (void)bmu_battery_manager_get_summary(mgr, &avg_mv, &total_current_a, NULL);
    return total_current_a;
}

float bmu_battery_manager_get_last_voltage_mv(bmu_battery_manager_t *mgr, int idx)
{
    if (mgr == NULL || idx < 0 || idx >= BMU_MAX_BATTERIES) return 0.0f;

    float value = 0.0f;
    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        value = mgr->last_voltage_mv[idx];
        xSemaphoreGive(mgr->mutex);
    }
    return value;
}

float bmu_battery_manager_get_last_current_a(bmu_battery_manager_t *mgr, int idx)
{
    if (mgr == NULL || idx < 0 || idx >= BMU_MAX_BATTERIES) return 0.0f;

    float value = 0.0f;
    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        value = mgr->last_current_a[idx];
        xSemaphoreGive(mgr->mutex);
    }
    return value;
}
