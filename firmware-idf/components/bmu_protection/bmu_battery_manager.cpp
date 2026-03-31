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
            float current_a = 0;
            if (bmu_ina237_read_current(&mgr->ina_devices[i], &current_a) != ESP_OK
                || std::isnan(current_a))
                continue;

            if (elapsed_h > 0) {
                if (current_a > 0)       mgr->ah_discharge[i] += current_a * elapsed_h;
                else if (current_a < 0)  mgr->ah_charge[i]    += (-current_a) * elapsed_h;
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

float bmu_battery_manager_get_max_voltage_mv(bmu_battery_manager_t *mgr)
{
    float max_mv = 0;
    for (int i = 0; i < mgr->nb_ina; i++) {
        float v = 0;
        if (bmu_ina237_read_bus_voltage(&mgr->ina_devices[i], &v) == ESP_OK && v > max_mv)
            max_mv = v;
    }
    return max_mv;
}

float bmu_battery_manager_get_min_voltage_mv(bmu_battery_manager_t *mgr)
{
    float min_mv = 999999;
    for (int i = 0; i < mgr->nb_ina; i++) {
        float v = 0;
        if (bmu_ina237_read_bus_voltage(&mgr->ina_devices[i], &v) == ESP_OK && v > 1000 && v < min_mv)
            min_mv = v;
    }
    return min_mv < 999999 ? min_mv : 0;
}

float bmu_battery_manager_get_avg_voltage_mv(bmu_battery_manager_t *mgr)
{
    float sum = 0;
    int n = 0;
    for (int i = 0; i < mgr->nb_ina; i++) {
        float v = 0;
        if (bmu_ina237_read_bus_voltage(&mgr->ina_devices[i], &v) == ESP_OK && v > 1000) {
            sum += v;
            n++;
        }
    }
    return n > 0 ? sum / n : 0;
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
    float total = 0;
    for (int i = 0; i < mgr->nb_ina; i++) {
        float c = 0;
        if (bmu_ina237_read_current(&mgr->ina_devices[i], &c) == ESP_OK && !std::isnan(c))
            total += c;
    }
    return total;
}
