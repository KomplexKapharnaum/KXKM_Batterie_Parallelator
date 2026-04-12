// firmware-idf-v2/components/bmu_mqtt/src/bmu_mqtt_replay.c
//
// Phase 16 -- Oneshot replay task: drain NOSYNC files over MQTT.

#include "bmu_mqtt.h"
#include "bmu_sd_log.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "mqtt_replay";

// Forward decl -- the MQTT client handle is in bmu_mqtt.c, we access via
// extern. Keeping it simple for Phase 16 (no accessor function overhead).
extern esp_mqtt_client_handle_t s_client;
extern char s_device_id[16];

static void replay_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "replay task starting");

    char topic[64];
    snprintf(topic, sizeof(topic), "bmu/%s/replay", s_device_id);

    bmu_sd_log_nosync_entry_t entries[8];
    int count = bmu_sd_log_list_nosync(entries, 8);
    ESP_LOGI(TAG, "found %d NOSYNC files to replay", count);

    for (int i = 0; i < count; i++) {
        ESP_LOGI(TAG, "replaying %s (%u bytes)",
                 entries[i].path, (unsigned)entries[i].file_size);

        FILE *fp = fopen(entries[i].path, "r");
        if (fp == NULL) {
            ESP_LOGW(TAG, "fopen(%s) failed, skipping", entries[i].path);
            continue;
        }

        char line[512];
        int batch = 0;
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (!bmu_mqtt_is_connected()) {
                ESP_LOGW(TAG, "MQTT disconnected during replay, aborting");
                fclose(fp);
                goto done;
            }

            // Remove trailing newline
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
                line[--len] = '\0';
            }
            if (len == 0) continue;

            esp_mqtt_client_publish(s_client, topic, line, (int)len, 1, 0);
            batch++;

            if (batch >= 50) {
                vTaskDelay(pdMS_TO_TICKS(10));
                batch = 0;
            }
        }
        fclose(fp);

        // Mark synced (rename removes -NOSYNC suffix)
        esp_err_t err = bmu_sd_log_mark_synced(entries[i].path);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "mark_synced failed for %s: %s",
                     entries[i].path, esp_err_to_name(err));
        }
    }

done:
    ESP_LOGI(TAG, "replay task done");
    vTaskDelete(NULL);
}

void bmu_mqtt_replay_start(void)
{
    if (!bmu_sd_log_is_ready()) {
        ESP_LOGW(TAG, "SD log not ready, skipping replay");
        return;
    }

    xTaskCreatePinnedToCore(replay_task, "mqtt_replay", 4096, NULL, 2, NULL, 1);
    ESP_LOGI(TAG, "replay task spawned");
}
