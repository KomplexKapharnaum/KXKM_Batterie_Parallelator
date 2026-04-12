// firmware-idf-v2/components/bmu_wifi/include/bmu_wifi.h
//
// Phase 16 -- Wi-Fi STA simple. Lit SSID/PSK depuis NVS namespace "bmu".
// Pas de SoftAP fallback, pas de supervisor task.

#pragma once
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Init Wi-Fi STA. Reads SSID/PSK from NVS namespace "bmu" keys "wifi_ssid"
// and "wifi_psk". Returns ESP_ERR_NVS_NOT_FOUND if no creds stored.
esp_err_t bmu_wifi_init(void);
bool bmu_wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
