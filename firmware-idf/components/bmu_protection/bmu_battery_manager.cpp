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

typedef struct {
    int idx;
    bmu_battery_manager_t *mgr;
} ah_task_ctx_t;

static void ah_task(void *pv)
{
    ah_task_ctx_t *ctx = (ah_task_ctx_t *)pv;
    const int idx = ctx->idx;
    bmu_battery_manager_t *mgr = ctx->mgr;
    free(ctx);

    float total_discharge = 0, total_charge = 0;
    int64_t last_us = esp_timer_get_time();
    int sample = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        float current_a = 0;
        esp_err_t ret = bmu_ina237_read_current(&mgr->ina_devices[idx], &current_a);
        if (ret != ESP_OK || isnan(current_a)) continue;

        int64_t now_us = esp_timer_get_time();
        float elapsed_h = (float)(now_us - last_us) / 3.6e9f;
        last_us = now_us;

        if (elapsed_h > 0) {
            if (current_a > 0) total_discharge += current_a * elapsed_h;
            else if (current_a < 0) total_charge += (-current_a) * elapsed_h;
        }

        if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            mgr->ah_discharge[idx] = total_discharge;
            mgr->ah_charge[idx] = total_charge;
            xSemaphoreGive(mgr->mutex);
        }

        if (++sample % 100 == 0) {
            ESP_LOGI(TAG, "BAT[%d] Ah discharge=%.3f charge=%.3f",
                     idx + 1, total_discharge, total_charge);
        }
    }
}

esp_err_t bmu_battery_manager_start_ah_task(bmu_battery_manager_t *mgr, int idx)
{
    if (idx < 0 || idx >= mgr->nb_ina) return ESP_ERR_INVALID_ARG;

    bool already = false;
    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        already = mgr->ah_running[idx];
        xSemaphoreGive(mgr->mutex);
    }
    if (already) return ESP_OK;

    ah_task_ctx_t *ctx = (ah_task_ctx_t *)malloc(sizeof(ah_task_ctx_t));
    if (!ctx) return ESP_ERR_NO_MEM;
    ctx->idx = idx;
    ctx->mgr = mgr;

    char name[16];
    snprintf(name, sizeof(name), "Ah_%d", idx);
    TaskHandle_t handle = NULL;
    if (xTaskCreate(ah_task, name, 3072, ctx, 1, &handle) != pdPASS) {
        free(ctx);
        return ESP_FAIL;
    }

    if (xSemaphoreTake(mgr->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        mgr->ah_tasks[idx] = handle;
        mgr->ah_running[idx] = true;
        xSemaphoreGive(mgr->mutex);
    }
    ESP_LOGI(TAG, "Ah task started for BAT[%d]", idx + 1);
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
        if (bmu_ina237_read_current(&mgr->ina_devices[i], &c) == ESP_OK && !isnan(c))
            total += c;
    }
    return total;
}
