// firmware-idf-v2/main/task_ui.cpp
//
// Phase 17 -- tache FreeRTOS de refresh UI.
// Initialise le tabview 5 onglets puis boucle a 5 Hz pour refresh data.

#include "task_ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

extern "C" {
#include "bmu_ui.h"
#include "bmu_core.h"
}

static const char *TAG = "task_ui";

static void task_ui_fn(void *arg)
{
    struct BmuCore *core = (struct BmuCore *)arg;

    // Petite attente pour laisser le splash visible
    vTaskDelay(pdMS_TO_TICKS(1500));

    esp_err_t err = bmu_ui_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmu_ui_init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UI task running (5 Hz refresh)");

    while (true) {
        bmu_ui_update_data(core);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void task_ui_start(struct BmuCore *core)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        task_ui_fn,
        "task_ui",
        8192,
        (void *)core,
        2,
        NULL,
        1  // APP_CPU
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore failed");
    } else {
        ESP_LOGI(TAG, "task_ui started on APP_CPU (prio=2, stack=8192)");
    }
}
