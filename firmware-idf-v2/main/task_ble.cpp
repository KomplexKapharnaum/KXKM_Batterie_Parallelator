// firmware-idf-v2/main/task_ble.cpp
//
// Phase 18 : tache BLE — init NimBLE + notify 2 Hz.

#include "task_ble.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "bmu_ble.h"
#include "bmu_core.h"
}

static const char *TAG = "task-ble";

/* Forward declaration from bmu_ble_gatt.c */
extern "C" void bmu_ble_gatt_set_core(struct BmuCore *core);

static void task_ble_func(void *param) {
    struct BmuCore *core = (struct BmuCore *)param;

    ESP_LOGI(TAG, "BLE task starting, init NimBLE...");
    esp_err_t err = bmu_ble_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmu_ble_init failed: %s -- task exits", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    /* Set core handle for GATT read callbacks */
    bmu_ble_gatt_set_core(core);

    /* Wait 2 seconds for NimBLE to sync before first notification */
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(TAG, "BLE notify loop started (2 Hz)");
    while (true) {
        bmu_ble_notify_all(core);
        vTaskDelay(pdMS_TO_TICKS(500));  /* 2 Hz */
    }
}

void task_ble_start(struct BmuCore *core) {
    BaseType_t ret = xTaskCreatePinnedToCore(
        task_ble_func,
        "task_ble",
        4096,
        core,
        3,
        NULL,
        1  /* APP_CPU */
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore failed");
    } else {
        ESP_LOGI(TAG, "task_ble created on APP_CPU, prio 3");
    }
}
