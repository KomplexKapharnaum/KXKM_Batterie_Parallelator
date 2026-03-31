#pragma once

#include "driver/i2c.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMU_I2C_SDA_GPIO    GPIO_NUM_41
#define BMU_I2C_SCL_GPIO    GPIO_NUM_40
#define BMU_I2C_PORT        I2C_NUM_1
#define BMU_I2C_FREQ_HZ     50000

/**
 * @brief Initialise le bus I2C BMU sur I2C_NUM_1 (old driver API).
 *
 * Utilise i2c_param_config + i2c_driver_install pour eviter le conflit
 * avec le BSP esp-box-3 qui utilise l'ancien driver sur I2C_NUM_0.
 */
esp_err_t bmu_i2c_init(void);

/**
 * @brief Scanne les plages TCA (0x20-0x27) et INA (0x40-0x4F).
 * @return nombre de devices detectes
 */
int bmu_i2c_scan(void);

/**
 * @brief Acquire the BMU I2C bus mutex. Must be called before multi-register operations.
 * @return ESP_OK if acquired, ESP_ERR_TIMEOUT if not acquired within 100ms.
 */
esp_err_t bmu_i2c_lock(void);

/**
 * @brief Release the BMU I2C bus mutex.
 */
void bmu_i2c_unlock(void);

/**
 * @brief Record a successful I2C operation (resets failure counter).
 */
void bmu_i2c_record_success(void);

/**
 * @brief Record an I2C failure. After 5 consecutive failures, triggers bus reset.
 */
void bmu_i2c_record_failure(void);

/**
 * @brief Write then read (old API wrapper using cmd_link).
 *
 * No mutex — callers are responsible for locking when needed.
 */
esp_err_t bmu_i2c_write_read(uint8_t addr, const uint8_t *write_buf, size_t write_len,
                              uint8_t *read_buf, size_t read_len, uint32_t timeout_ms);

/**
 * @brief Write only (old API wrapper using cmd_link).
 *
 * No mutex — callers are responsible for locking when needed.
 */
esp_err_t bmu_i2c_write(uint8_t addr, const uint8_t *buf, size_t len, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
