// firmware-idf-v2/components/bmu_wifi/src/bmu_wifi.c
//
// Phase 16 -- Wi-Fi STA simple avec exponential backoff.
// Heritage v1 (bmu_wifi.cpp) simplifie : pure C, pas de SoftAP, pas de supervisor.

#include "bmu_wifi.h"

#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "wifi";

#define BIT_CONNECTED BIT0

static EventGroupHandle_t s_wifi_events = NULL;
static int s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_num++;
        int delay_s = 1;
        if (s_retry_num >= 2) delay_s = 5;
        if (s_retry_num >= 3) delay_s = 25;
        ESP_LOGW(TAG, "disconnected, retry #%d in %ds", s_retry_num, delay_s);
        vTaskDelay(pdMS_TO_TICKS(delay_s * 1000));
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_events, BIT_CONNECTED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_events, BIT_CONNECTED);
    }
}

esp_err_t bmu_wifi_init(void)
{
    // Read creds from NVS
    char ssid[33] = {0};
    char psk[65] = {0};

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("bmu", NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace 'bmu' not found: %s", esp_err_to_name(err));
        return ESP_ERR_NVS_NOT_FOUND;
    }

    size_t ssid_len = sizeof(ssid);
    err = nvs_get_str(nvs, "wifi_ssid", ssid, &ssid_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS key 'wifi_ssid' not found: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return ESP_ERR_NVS_NOT_FOUND;
    }

    size_t psk_len = sizeof(psk);
    err = nvs_get_str(nvs, "wifi_psk", psk, &psk_len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS key 'wifi_psk' not found: %s", esp_err_to_name(err));
        nvs_close(nvs);
        return ESP_ERR_NVS_NOT_FOUND;
    }
    nvs_close(nvs);

    ESP_LOGI(TAG, "NVS creds loaded: SSID='%s'", ssid);

    // Init netif + event loop + WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    // Reduced buffers for BLE coex
    wifi_cfg.static_rx_buf_num = 6;
    wifi_cfg.dynamic_rx_buf_num = 16;
    wifi_cfg.dynamic_tx_buf_num = 16;
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    s_wifi_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t sta_cfg = {0};
    memcpy(sta_cfg.sta.ssid, ssid, strlen(ssid));
    memcpy(sta_cfg.sta.password, psk, strlen(psk));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi STA started, connecting to '%s'", ssid);
    return ESP_OK;
}

bool bmu_wifi_is_connected(void)
{
    if (s_wifi_events == NULL) return false;
    return (xEventGroupGetBits(s_wifi_events) & BIT_CONNECTED) != 0;
}

esp_err_t bmu_wifi_set_creds(const char *ssid, const char *psk)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open("bmu", NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, "wifi_ssid", ssid);
    if (err != ESP_OK) { nvs_close(h); return err; }

    err = nvs_set_str(h, "wifi_psk", psk);
    if (err != ESP_OK) { nvs_close(h); return err; }

    err = nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "credentials stored in NVS (ssid='%s')", ssid);
    return err;
}
