/*
 * bmu_i2c_glue — C layer between ESP-IDF i2c_master driver and the Rust core's
 * BmuRawInputs. No parsing: raw register dumps only. The Rust core's
 * bmu-drivers::ina237::parse_vbus/parse_current/parse_dietemp_c10 do all the
 * conversion.
 *
 * Hardware (KXKM PCB v2, ESP32-S3-BOX-3 DOCK bus):
 *   - I²C port : I2C_NUM_1 (BSP-owned, retrieved via i2c_master_get_bus_handle)
 *   - SDA      : GPIO 41
 *   - SCL      : GPIO 40
 *   - Freq     : 100 kHz
 *   - INA237   : 0x40..0x4F (16 devices max)
 *   - TCA9535  : 0x20..0x23 (4 devices max, 4 channels each = 16 batteries)
 *   - AHT30    : 0x38 (single device)
 *
 * Thread-safety: all reads go through a FreeRTOS semaphore internally.
 * Callers do NOT need to hold a lock themselves.
 *
 * Topology contract: Nb_TCA × 4 == Nb_INA (enforced by Rust core).
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2c_types.h"
#include "bmu_core.h"   // BmuRawInputs

#ifdef __cplusplus
extern "C" {
#endif

// --- Hardware constants (match firmware-idf v1 archive) ---
#define BMU_I2C_PORT              I2C_NUM_1
#define BMU_I2C_SDA_GPIO          GPIO_NUM_41
#define BMU_I2C_SCL_GPIO          GPIO_NUM_40
#define BMU_I2C_FREQ_HZ           100000

#define BMU_INA237_ADDR_BASE      0x40u
#define BMU_INA237_ADDR_MAX       0x4Fu
#define BMU_INA237_REG_DUMP_LEN   18u

#define BMU_TCA9535_ADDR_BASE     0x20u
#define BMU_TCA9535_ADDR_MAX      0x23u
#define BMU_TCA9535_INPUT_PORT0   0x00u

#define BMU_AHT30_ADDR            0x38u
#define BMU_AHT30_MEAS_WAIT_MS    80u

#define BMU_I2C_DEVICE_TIMEOUT_MS 50

/**
 * Initialize the glue: retrieve BSP bus handle, create the bus lock semaphore.
 * Device handles are created lazily in bmu_i2c_glue_scan().
 *
 * @return ESP_OK on success.
 */
esp_err_t bmu_i2c_glue_init(void);

/**
 * Scan all INA237 and TCA9535 candidate addresses via address probe.
 * Populates `out_n_ina` and `out_n_tca` with actually responding devices.
 *
 * @param out_n_ina Number of INA237 detected (0..16)
 * @param out_n_tca Number of TCA9535 detected (0..4)
 * @return ESP_OK on success (even if 0 devices found)
 */
esp_err_t bmu_i2c_glue_scan(uint8_t *out_n_ina, uint8_t *out_n_tca);

/**
 * Read ALL inputs into a BmuRawInputs struct, ready to pass to bmu_core_tick.
 * Fills n_ina, n_tca, ina_registers, tca_inputs, climate_*, monotonic_us.
 *
 * On per-device failure, that device's slot is zeroed (TCA defaults to 0xFF
 * fail-safe) and the function continues.
 *
 * @param out Target struct (must be non-NULL)
 * @return ESP_OK on success.
 */
esp_err_t bmu_i2c_glue_read_inputs(struct BmuRawInputs *out);

/**
 * Bus recovery sequence stub — real implementation in Phase 14 Task 14.3.
 *
 * @return ESP_OK on success
 */
esp_err_t bmu_i2c_glue_recover_bus(void);

#ifdef __cplusplus
}
#endif
