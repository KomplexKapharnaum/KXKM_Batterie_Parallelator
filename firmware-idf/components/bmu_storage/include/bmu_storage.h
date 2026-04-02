#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── SD Card (SPI mode on BOX-3 PMOD2) ──────────────────────────────── */
// PMOD2 pinout: MOSI=GPIO11, MISO=GPIO13, CLK=GPIO12, CS=GPIO10
#define BMU_SD_MOSI     GPIO_NUM_11
#define BMU_SD_MISO     GPIO_NUM_13
#define BMU_SD_CLK      GPIO_NUM_12
#define BMU_SD_CS       GPIO_NUM_10
#define BMU_SD_MOUNT    "/sdcard"

esp_err_t bmu_sd_init(void);
bool bmu_sd_is_mounted(void);
esp_err_t bmu_sd_log_line(const char *line);
esp_err_t bmu_sd_read_last_lines(char *buf, size_t buf_size, int max_lines);

/* ── Internal FAT partition (USB-accessible config) ──────────────────── */
#define BMU_FAT_MOUNT   "/fatfs"

esp_err_t bmu_fat_init(void);
bool bmu_fat_is_mounted(void);

/* ── USB Mass Storage (TinyUSB MSC) ──────────────────────────────────── */
esp_err_t bmu_usb_msc_init(void);

/* ── NVS (credentials + config) ──────────────────────────────────────── */
esp_err_t bmu_nvs_init(void);
esp_err_t bmu_nvs_get_str(const char *key, char *buf, size_t len);
esp_err_t bmu_nvs_set_str(const char *key, const char *value);

#ifdef __cplusplus
}
#endif
