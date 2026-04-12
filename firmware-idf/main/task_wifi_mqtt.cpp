// firmware-idf-v2/main/task_wifi_mqtt.cpp
//
// Phase 16 -- Tache Wi-Fi + MQTT : init WiFi STA, attend IP, init MQTT,
// publie telemetrie 1 Hz.

#include "task_wifi_mqtt.h"

extern "C" {
#include "bmu_wifi.h"
#include "bmu_mqtt.h"
#include "bmu_core.h"
#include "nvs.h"
}

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "wifi_mqtt";

static void task_body(void *arg)
{
    BmuCore *core = (BmuCore *)arg;
    ESP_LOGI(TAG, "task_wifi_mqtt starting");

    // Wait for BLE controller to finish init (avoid HCI race)
    vTaskDelay(pdMS_TO_TICKS(3000));

    esp_err_t err = bmu_wifi_init();
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "no WiFi creds in NVS, waiting for BLE prov (Phase 20)");
        // Stay alive but idle -- Phase 20 will set creds and reboot
        while (true) vTaskDelay(pdMS_TO_TICKS(10000));
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bmu_wifi_init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // Wait for IP (max 30s)
    for (int i = 0; i < 120 && !bmu_wifi_is_connected(); i++) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (!bmu_wifi_is_connected()) {
        ESP_LOGW(TAG, "WiFi connect timeout, will keep retrying in background");
    }

    bmu_mqtt_init();
    bmu_mqtt_start();

    TickType_t last = xTaskGetTickCount();
    for (;;) {
        if (bmu_mqtt_is_connected()) {
            bmu_mqtt_publish_telemetry(core);
        }
        vTaskDelayUntil(&last, pdMS_TO_TICKS(1000));
    }
}

extern "C" void task_wifi_mqtt_start(struct BmuCore *core)
{
    xTaskCreatePinnedToCore(task_body, "wifi_mqtt", 8192, (void *)core, 3, NULL, 1);
    ESP_LOGI(TAG, "task created pinned APP_CPU prio 3");
}
