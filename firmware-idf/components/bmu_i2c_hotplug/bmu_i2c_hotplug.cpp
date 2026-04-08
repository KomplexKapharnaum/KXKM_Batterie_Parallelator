#include "bmu_i2c_hotplug.h"
#include "bmu_i2c.h"
#include "bmu_config.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HOTPLUG";

static bmu_hotplug_cfg_t   s_cfg = {};
static bmu_hotplug_stats_t s_stats = {};
static TaskHandle_t        s_task = NULL;
static bool                s_initialized = false;

esp_err_t bmu_hotplug_init(const bmu_hotplug_cfg_t *cfg)
{
    if (cfg == NULL || cfg->bus == NULL) return ESP_ERR_INVALID_ARG;
    s_cfg = *cfg;
    s_initialized = true;
    ESP_LOGI(TAG, "Hotplug initialized — interval %ds", CONFIG_BMU_I2C_HOTPLUG_INTERVAL_S);
    return ESP_OK;
}

static void hotplug_task(void *pv);

esp_err_t bmu_hotplug_start(void)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (s_task != NULL) return ESP_ERR_INVALID_STATE;

    BaseType_t ret = xTaskCreate(hotplug_task, "hotplug",
                                 CONFIG_BMU_I2C_HOTPLUG_STACK_SIZE,
                                 NULL, CONFIG_BMU_I2C_HOTPLUG_TASK_PRIORITY,
                                 &s_task);
    return (ret == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t bmu_hotplug_stop(void)
{
    if (s_task != NULL) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    return ESP_OK;
}

void bmu_hotplug_get_stats(bmu_hotplug_stats_t *stats)
{
    if (stats) *stats = s_stats;
}

/* Placeholder — full scan implemented in Task 3 */
static void hotplug_task(void *pv)
{
    const TickType_t period = pdMS_TO_TICKS(CONFIG_BMU_I2C_HOTPLUG_INTERVAL_S * 1000);
    for (;;) {
        vTaskDelay(period);
        s_stats.scan_count++;
        ESP_LOGD(TAG, "Scan #%lu (stub)", (unsigned long)s_stats.scan_count);
    }
}
