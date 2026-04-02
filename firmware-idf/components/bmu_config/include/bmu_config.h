#pragma once

#include "sdkconfig.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Kconfig compile-time defaults (inchangé) ──────────────────────── */
#define BMU_MIN_VOLTAGE_MV      CONFIG_BMU_MIN_VOLTAGE_MV
#define BMU_MAX_VOLTAGE_MV      CONFIG_BMU_MAX_VOLTAGE_MV
#define BMU_MAX_CURRENT_MA      CONFIG_BMU_MAX_CURRENT_MA
#define BMU_VOLTAGE_DIFF_MV     CONFIG_BMU_VOLTAGE_DIFF_MV
#define BMU_RECONNECT_DELAY_MS  CONFIG_BMU_RECONNECT_DELAY_MS
#define BMU_NB_SWITCH_MAX       CONFIG_BMU_NB_SWITCH_MAX
#define BMU_OVERCURRENT_FACTOR  CONFIG_BMU_OVERCURRENT_FACTOR
#define BMU_LOOP_PERIOD_MS      CONFIG_BMU_LOOP_PERIOD_MS

#define BMU_MAX_BATTERIES       16
#define BMU_MAX_TCA             8

/* ── Limites buffers ───────────────────────────────────────────────── */
#define BMU_CONFIG_NAME_MAX     32
#define BMU_CONFIG_SSID_MAX     33   /* 802.11 max SSID = 32 + NUL */
#define BMU_CONFIG_PASS_MAX     64
#define BMU_CONFIG_URI_MAX      128

/* ── Init / log ────────────────────────────────────────────────────── */

/**
 * @brief Charge toute la config depuis NVS namespace "bmu".
 *        Si une cle est absente, le defaut Kconfig est conserve.
 *        Appeler UNE FOIS apres bmu_nvs_init().
 */
esp_err_t bmu_config_load(void);

/** Affiche la config courante (runtime, pas Kconfig brut). */
void bmu_config_log(void);

/* ── Device identity ───────────────────────────────────────────────── */
esp_err_t   bmu_config_set_device_name(const char *name);
const char *bmu_config_get_device_name(void);

/* ── WiFi ──────────────────────────────────────────────────────────── */
esp_err_t   bmu_config_set_wifi(const char *ssid, const char *password);
const char *bmu_config_get_wifi_ssid(void);
const char *bmu_config_get_wifi_password(void);

/* ── Protection thresholds (runtime override des defauts Kconfig) ── */
esp_err_t bmu_config_set_thresholds(uint16_t min_mv, uint16_t max_mv,
                                     uint16_t max_ma, uint16_t diff_mv);
void      bmu_config_get_thresholds(uint16_t *min_mv, uint16_t *max_mv,
                                     uint16_t *max_ma, uint16_t *diff_mv);

/* ── MQTT broker URI ───────────────────────────────────────────────── */
esp_err_t   bmu_config_set_mqtt_uri(const char *uri);
const char *bmu_config_get_mqtt_uri(void);

#ifdef __cplusplus
}
#endif
