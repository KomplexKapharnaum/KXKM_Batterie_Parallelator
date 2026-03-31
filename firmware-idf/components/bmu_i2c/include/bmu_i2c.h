#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BMU_I2C_SDA_GPIO    GPIO_NUM_41
#define BMU_I2C_SCL_GPIO    GPIO_NUM_40
#define BMU_I2C_PORT        I2C_NUM_1
#define BMU_I2C_FREQ_HZ     50000

esp_err_t bmu_i2c_init(i2c_master_bus_handle_t *bus_handle);
esp_err_t bmu_i2c_add_device(i2c_master_bus_handle_t bus, uint8_t addr,
                              i2c_master_dev_handle_t *dev);
int bmu_i2c_scan(i2c_master_bus_handle_t bus);

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

#ifdef __cplusplus
}
#endif
