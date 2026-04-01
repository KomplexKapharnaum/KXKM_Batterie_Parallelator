/**
 * @file bmu_wifi.cpp
 * @brief Composant WiFi STA pour BMU — connexion automatique avec reconnexion
 *
 * Utilise les paramètres Kconfig (BMU_WIFI_SSID, BMU_WIFI_PASSWORD, BMU_WIFI_MAX_RETRY).
 * Si BMU_WIFI_MAX_RETRY == 0, la reconnexion est infinie.
 */

#include "bmu_wifi.h"

#include <cstring>
#include <cstdio>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "WIFI";

// ── Bit EventGroup ──────────────────────────────────────────────────────────
#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_event_group = nullptr;
static int s_retry_count = 0;
static char s_ip_addr[16] = {0};

// ── Handlers d'événements ───────────────────────────────────────────────────

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA démarré, tentative de connexion…");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        s_ip_addr[0] = '\0';

        int max_retry = CONFIG_BMU_WIFI_MAX_RETRY;
        bool should_retry = (max_retry == 0) || (s_retry_count < max_retry);

        if (should_retry) {
            s_retry_count++;
            ESP_LOGW(TAG, "Déconnecté — reconnexion dans 2 s (tentative %d%s)",
                     s_retry_count,
                     max_retry == 0 ? "/∞" : "");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
        } else {
            ESP_LOGE(TAG, "Nombre max de tentatives atteint (%d) — abandon", max_retry);
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connecté — IP : %s", s_ip_addr);
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ── API publique ────────────────────────────────────────────────────────────

esp_err_t bmu_wifi_init(void)
{
    // Initialiser NVS (requis par esp_wifi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS : effacement et ré-initialisation");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == nullptr) {
        ESP_LOGE(TAG, "Impossible de créer l'EventGroup");
        return ESP_ERR_NO_MEM;
    }

    // Pile réseau
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Configuration WiFi — buffers réduits pour coex BLE
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.static_rx_buf_num = 4;       // 10 → 4 (coex BLE)
    cfg.dynamic_rx_buf_num = 16;     // 32 → 16
    cfg.dynamic_tx_buf_num = 16;     // 32 → 16
    esp_err_t wifi_ret = esp_wifi_init(&cfg);
    if (wifi_ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi init failed: %s — running sans WiFi",
                 esp_err_to_name(wifi_ret));
        return wifi_ret;
    }

    // Enregistrement des handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    // Configuration STA
    wifi_config_t wifi_config = {};
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid),
                 CONFIG_BMU_WIFI_SSID,
                 sizeof(wifi_config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.password),
                 CONFIG_BMU_WIFI_PASSWORD,
                 sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode =
        (std::strlen(CONFIG_BMU_WIFI_PASSWORD) > 0) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;  /* Support WPA3 transition */

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Init terminée — SSID : %s", CONFIG_BMU_WIFI_SSID);
    return ESP_OK;
}

bool bmu_wifi_is_connected(void)
{
    if (s_wifi_event_group == nullptr) {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

esp_err_t bmu_wifi_get_ip(char *buf, size_t len)
{
    if (buf == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bmu_wifi_is_connected() || s_ip_addr[0] == '\0') {
        buf[0] = '\0';
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    std::strncpy(buf, s_ip_addr, len - 1);
    buf[len - 1] = '\0';
    return ESP_OK;
}

esp_err_t bmu_wifi_get_rssi(int8_t *rssi)
{
    if (!bmu_wifi_is_connected() || rssi == nullptr) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        *rssi = ap_info.rssi;
    }
    return ret;
}
