#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bmu_wifi_init(void);
bool bmu_wifi_is_connected(void);
esp_err_t bmu_wifi_get_ip(char *buf, size_t len);

/**
 * @brief Get current WiFi RSSI (signal strength).
 * @param[out] rssi RSSI in dBm (negative value, e.g. -65).
 * @return ESP_OK if connected, ESP_ERR_WIFI_NOT_CONNECT if not.
 */
esp_err_t bmu_wifi_get_rssi(int8_t *rssi);

#ifdef __cplusplus
}
#endif
