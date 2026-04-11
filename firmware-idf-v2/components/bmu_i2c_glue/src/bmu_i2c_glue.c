/*
 * See bmu_i2c_glue.h for contract.
 *
 * Phase 13: implements init + scan + read_inputs for INA237 (VBUS/CURRENT),
 * TCA9535 (INPUT_PORT0) and AHT30 (trig+wait+6 bytes Q20 parse).
 * Phase 14 will add bus recovery + watchdog retry policy.
 */

#include "bmu_i2c_glue.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "i2c-glue";

static i2c_master_bus_handle_t s_bus = NULL;
static SemaphoreHandle_t       s_lock = NULL;

// Per-address device handles (lazily created in scan)
static i2c_master_dev_handle_t s_ina_handles[16] = {NULL};
static i2c_master_dev_handle_t s_tca_handles[4]  = {NULL};
static i2c_master_dev_handle_t s_aht_handle      = NULL;

// Topology state (populated by scan, consumed by read_inputs)
static uint8_t s_n_ina = 0;
static uint8_t s_n_tca = 0;

esp_err_t bmu_i2c_glue_init(void) {
    if (s_bus != NULL) {
        ESP_LOGW(TAG, "already initialized");
        return ESP_OK;
    }

    // Retrieve BSP-owned bus handle (BOX-3 BSP has already called bsp_i2c_init)
    esp_err_t err = i2c_master_get_bus_handle(BMU_I2C_PORT, &s_bus);
    if (err != ESP_OK || s_bus == NULL) {
        ESP_LOGE(TAG, "i2c_master_get_bus_handle failed: %s", esp_err_to_name(err));
        return err;
    }

    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        ESP_LOGE(TAG, "mutex create failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "initialized (bus handle=%p)", s_bus);
    return ESP_OK;
}

static esp_err_t create_dev_handle(uint8_t addr, i2c_master_dev_handle_t *out) {
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BMU_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(s_bus, &dev_cfg, out);
}

esp_err_t bmu_i2c_glue_scan(uint8_t *out_n_ina, uint8_t *out_n_tca) {
    if (s_bus == NULL) return ESP_ERR_INVALID_STATE;
    if (out_n_ina == NULL || out_n_tca == NULL) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    s_n_ina = 0;
    s_n_tca = 0;

    // Scan INA237 range
    for (uint8_t a = BMU_INA237_ADDR_BASE; a <= BMU_INA237_ADDR_MAX; a++) {
        esp_err_t probe = i2c_master_probe(s_bus, a, 100);
        if (probe == ESP_OK) {
            uint8_t idx = a - BMU_INA237_ADDR_BASE;
            if (s_ina_handles[idx] == NULL) {
                create_dev_handle(a, &s_ina_handles[idx]);
            }
            s_n_ina++;
        }
    }

    // Scan TCA9535 range
    for (uint8_t a = BMU_TCA9535_ADDR_BASE; a <= BMU_TCA9535_ADDR_MAX; a++) {
        esp_err_t probe = i2c_master_probe(s_bus, a, 100);
        if (probe == ESP_OK) {
            uint8_t idx = a - BMU_TCA9535_ADDR_BASE;
            if (s_tca_handles[idx] == NULL) {
                create_dev_handle(a, &s_tca_handles[idx]);
            }
            s_n_tca++;
        }
    }

    // AHT30 (single address)
    esp_err_t aht_probe = i2c_master_probe(s_bus, BMU_AHT30_ADDR, 100);
    if (aht_probe == ESP_OK && s_aht_handle == NULL) {
        create_dev_handle(BMU_AHT30_ADDR, &s_aht_handle);
    }

    xSemaphoreGive(s_lock);

    *out_n_ina = s_n_ina;
    *out_n_tca = s_n_tca;

    ESP_LOGI(TAG, "scan result: INA=%u TCA=%u AHT=%s",
             s_n_ina, s_n_tca, (aht_probe == ESP_OK) ? "yes" : "no");
    return ESP_OK;
}

/**
 * Read VBUS (reg 0x05) + CURRENT (reg 0x07) into a 18-byte buffer formatted
 * for BmuRawInputs consumption:
 *   bytes[0..2] = VBUS big-endian (direct from INA237)
 *   bytes[2..4] = CURRENT big-endian
 *   bytes[4..18] = zero (reserved for future SHUNT/DIETEMP/POWER reads)
 *
 * Returns ESP_OK on both reads success. On any error, the full buffer is zeroed.
 */
static esp_err_t read_ina237_raw(i2c_master_dev_handle_t dev, uint8_t out[18]) {
    memset(out, 0, 18);

    // Read VBUS (2 bytes)
    uint8_t reg_vbus = 0x05;
    esp_err_t err = i2c_master_transmit_receive(
        dev, &reg_vbus, 1, &out[0], 2,
        BMU_I2C_DEVICE_TIMEOUT_MS
    );
    if (err != ESP_OK) return err;

    // Read CURRENT (2 bytes)
    uint8_t reg_current = 0x07;
    err = i2c_master_transmit_receive(
        dev, &reg_current, 1, &out[2], 2,
        BMU_I2C_DEVICE_TIMEOUT_MS
    );
    if (err != ESP_OK) {
        memset(out, 0, 18);
        return err;
    }

    return ESP_OK;
}

/**
 * Read INPUT_PORT0 (reg 0x00) of a TCA9535. On failure, *out = 0xFF (fail-safe).
 */
static esp_err_t read_tca9535_inputs(i2c_master_dev_handle_t dev, uint8_t *out) {
    uint8_t reg = BMU_TCA9535_INPUT_PORT0;
    esp_err_t err = i2c_master_transmit_receive(
        dev, &reg, 1, out, 1,
        BMU_I2C_DEVICE_TIMEOUT_MS
    );
    if (err != ESP_OK) {
        *out = 0xFF;  // fail-safe: all alerts inactive
    }
    return err;
}

/**
 * Trigger AHT30 measurement, wait 80 ms, read 6-byte result. Parses into
 * temp_c10 / rh_pct10 using the Q20 fixed-point math from the AHT30 datasheet.
 *
 * Returns ESP_OK on success. On failure, *out_temp and *out_rh are left unchanged.
 */
static esp_err_t read_aht30(i2c_master_dev_handle_t dev,
                             int16_t *out_temp_c10,
                             uint16_t *out_rh_pct10) {
    static const uint8_t CMD_TRIGGER[3] = {0xAC, 0x33, 0x00};
    uint8_t raw[6] = {0};

    esp_err_t err = i2c_master_transmit(dev, CMD_TRIGGER, 3, BMU_I2C_DEVICE_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    vTaskDelay(pdMS_TO_TICKS(BMU_AHT30_MEAS_WAIT_MS));

    err = i2c_master_receive(dev, raw, 6, BMU_I2C_DEVICE_TIMEOUT_MS);
    if (err != ESP_OK) return err;

    if (raw[0] & 0x80) return ESP_ERR_INVALID_RESPONSE;

    // Parse 20-bit RH: raw[1] << 12 | raw[2] << 4 | raw[3] >> 4
    uint32_t rh_raw = ((uint32_t)raw[1] << 12) |
                      ((uint32_t)raw[2] << 4)  |
                      ((uint32_t)raw[3] >> 4);
    // Parse 20-bit temp: (raw[3] & 0x0F) << 16 | raw[4] << 8 | raw[5]
    uint32_t t_raw = (((uint32_t)raw[3] & 0x0F) << 16) |
                     ((uint32_t)raw[4] << 8) |
                     (uint32_t)raw[5];

    *out_rh_pct10 = (uint16_t)(((uint64_t)rh_raw * 1000u) >> 20);
    int32_t t_scaled = (int32_t)(((uint64_t)t_raw * 2000u) >> 20);
    *out_temp_c10 = (int16_t)(t_scaled - 500);

    return ESP_OK;
}

esp_err_t bmu_i2c_glue_read_inputs(struct BmuRawInputs *out) {
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (s_bus == NULL) return ESP_ERR_INVALID_STATE;

    memset(out, 0, sizeof(*out));
    out->monotonic_us = (uint64_t)esp_timer_get_time();
    out->n_ina = s_n_ina;
    out->n_tca = s_n_tca;

    xSemaphoreTake(s_lock, portMAX_DELAY);

    // Read all detected INA237s
    for (uint8_t i = 0; i < s_n_ina && i < 16; i++) {
        if (s_ina_handles[i] == NULL) continue;
        esp_err_t err = read_ina237_raw(s_ina_handles[i], out->ina_registers[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "INA237[0x%02X] read failed: %s",
                     BMU_INA237_ADDR_BASE + i, esp_err_to_name(err));
        }
    }

    // Read all detected TCA9535s
    for (uint8_t i = 0; i < s_n_tca && i < 4; i++) {
        if (s_tca_handles[i] == NULL) continue;
        esp_err_t err = read_tca9535_inputs(s_tca_handles[i], &out->tca_inputs[i]);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "TCA9535[0x%02X] read failed: %s",
                     BMU_TCA9535_ADDR_BASE + i, esp_err_to_name(err));
        }
    }

    // Read AHT30 climate (non-critical — ignore failures)
    if (s_aht_handle != NULL) {
        int16_t t = 0;
        uint16_t rh = 0;
        if (read_aht30(s_aht_handle, &t, &rh) == ESP_OK) {
            out->climate_temp_c10 = t;
            out->climate_rh_pct10 = rh;
        }
    }

    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t bmu_i2c_glue_recover_bus(void) {
    ESP_LOGW(TAG, "recover_bus stub -- implemented in Phase 14 Task 14.3");
    return ESP_OK;
}
