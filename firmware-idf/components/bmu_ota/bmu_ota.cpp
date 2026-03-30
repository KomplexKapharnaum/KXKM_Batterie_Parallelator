#include "bmu_ota.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "esp_system.h"
#include <cstring>

static const char *TAG = "OTA";

esp_err_t bmu_ota_start(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        ESP_LOGE(TAG, "OTA URL is empty");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting OTA from: %s", url);

    esp_http_client_config_t http_config = {};
    http_config.url = url;
    http_config.timeout_ms = 30000;
    // For production: set http_config.cert_pem to server CA cert
    // For dev: http_config.skip_cert_common_name_check = true;

    esp_https_ota_config_t ota_config = {};
    ota_config.http_config = &http_config;

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA success — rebooting...");
        esp_restart();
        // Never reaches here
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t bmu_ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    esp_err_t ret = esp_ota_get_state_partition(running, &state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Could not get partition state: %s", esp_err_to_name(ret));
        return ret;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ret = esp_ota_mark_app_valid_cancel_rollback();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Firmware marked as valid");
        } else {
            ESP_LOGE(TAG, "Failed to mark valid: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    ESP_LOGI(TAG, "Firmware already validated (state=%d)", state);
    return ESP_OK;
}

esp_err_t bmu_ota_rollback(void)
{
    ESP_LOGW(TAG, "Rolling back to previous firmware...");
    esp_err_t ret = esp_ota_mark_app_invalid_rollback_and_reboot();
    // Won't return on success
    ESP_LOGE(TAG, "Rollback failed: %s", esp_err_to_name(ret));
    return ret;
}

esp_err_t bmu_ota_get_info(char *version, size_t len, char *date, size_t date_len)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    if (version && len > 0) {
        strncpy(version, desc->version, len - 1);
        version[len - 1] = '\0';
    }
    if (date && date_len > 0) {
        snprintf(date, date_len, "%s %s", desc->date, desc->time);
    }
    ESP_LOGI(TAG, "Running: %s (%s %s)", desc->version, desc->date, desc->time);
    return ESP_OK;
}
