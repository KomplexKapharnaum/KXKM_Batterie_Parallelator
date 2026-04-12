#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start OTA update from a URL.
 * Downloads firmware binary and flashes to inactive OTA partition.
 * Reboots on success.
 * @param url Full URL to .bin file (e.g. "https://kxkm-ai/ota/kxkm-bmu.bin")
 * @return ESP_OK on success (won't return — reboots), error code on failure.
 */
esp_err_t bmu_ota_start(const char *url);

/**
 * @brief Mark current firmware as valid after successful boot.
 * Must be called after verifying the new firmware works correctly.
 * If not called, the bootloader will rollback to the previous version on next reboot.
 */
esp_err_t bmu_ota_mark_valid(void);

/**
 * @brief Rollback to previous firmware version.
 * Marks current partition as invalid and reboots.
 */
esp_err_t bmu_ota_rollback(void);

/**
 * @brief Get info about current running firmware.
 */
esp_err_t bmu_ota_get_info(char *version, size_t len, char *date, size_t date_len);

#ifdef __cplusplus
}
#endif
