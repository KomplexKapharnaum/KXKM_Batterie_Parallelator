#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bmu_i2c_bb_ctx *bmu_i2c_bb_handle_t;

typedef struct {
    int      sda_gpio;
    int      scl_gpio;
    uint32_t freq_hz;   /* target frequency (actual may be lower) */
} bmu_i2c_bb_config_t;

/**
 * @brief Init bit-bang I2C bus. Configures GPIOs as open-drain.
 */
esp_err_t bmu_i2c_bb_init(const bmu_i2c_bb_config_t *cfg,
                           bmu_i2c_bb_handle_t *out_handle);

/**
 * @brief Write then read (combined transaction).
 *        write_len=0 for read-only, read_len=0 for write-only.
 */
esp_err_t bmu_i2c_bb_write_read(bmu_i2c_bb_handle_t handle,
                                 uint8_t addr,
                                 const uint8_t *write_buf,
                                 size_t write_len,
                                 uint8_t *read_buf,
                                 size_t read_len);

/**
 * @brief Write-only convenience.
 */
esp_err_t bmu_i2c_bb_write(bmu_i2c_bb_handle_t handle,
                            uint8_t addr,
                            const uint8_t *buf,
                            size_t len);

/**
 * @brief Scan address range, returns true if device ACKs.
 */
bool bmu_i2c_bb_probe(bmu_i2c_bb_handle_t handle, uint8_t addr);

/**
 * @brief Read a 16-bit register (big-endian, INA237/TCA style).
 */
esp_err_t bmu_i2c_bb_read_reg16(bmu_i2c_bb_handle_t handle,
                                 uint8_t addr, uint8_t reg,
                                 uint16_t *value);

/**
 * @brief Write a 16-bit register (big-endian).
 */
esp_err_t bmu_i2c_bb_write_reg16(bmu_i2c_bb_handle_t handle,
                                  uint8_t addr, uint8_t reg,
                                  uint16_t value);

#ifdef __cplusplus
}
#endif
