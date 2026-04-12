#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** État courant du module WiFi. */
typedef enum {
    BMU_WIFI_STATE_IDLE,        /**< init pas encore appelé */
    BMU_WIFI_STATE_CONNECTING,  /**< STA en cours de connexion */
    BMU_WIFI_STATE_CONNECTED,   /**< STA connecté avec IP */
    BMU_WIFI_STATE_AP_FALLBACK  /**< STA fail N fois → APSTA actif */
} bmu_wifi_state_t;

/** Allocate WiFi buffers + configure STA (no radio yet). */
esp_err_t bmu_wifi_init(void);

/** Start WiFi radio — call AFTER BLE init for coex. */
esp_err_t bmu_wifi_start(void);

bool bmu_wifi_is_connected(void);
esp_err_t bmu_wifi_get_ip(char *buf, size_t len);

/**
 * @brief Get current WiFi RSSI (signal strength).
 * @param[out] rssi RSSI in dBm (negative value, e.g. -65).
 * @return ESP_OK if connected, ESP_ERR_WIFI_NOT_CONNECT if not.
 */
esp_err_t bmu_wifi_get_rssi(int8_t *rssi);

/** Retourne l'état courant de la state machine WiFi. */
bmu_wifi_state_t bmu_wifi_get_state(void);

/** Retourne true si le softAP APSTA est actif. */
bool bmu_wifi_is_ap_active(void);

/**
 * @brief Retourne le SSID du softAP actif (vide si pas en fallback).
 * @param[out] buf  buffer destination
 * @param[in]  len  taille du buffer
 * @return ESP_OK toujours (retourne chaîne vide si pas en fallback)
 */
esp_err_t bmu_wifi_get_ap_ssid(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
