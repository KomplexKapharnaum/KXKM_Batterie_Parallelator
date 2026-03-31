/**
 * @file bmu_i2c.cpp
 * @brief Bus I2C BMU — old driver API (i2c_driver_install / i2c_cmd_link)
 *
 * Utilise l'ancien driver I2C ESP-IDF pour eviter le conflit avec le BSP
 * esp-box-3 qui utilise aussi l'ancien driver sur I2C_NUM_0.
 * Le bus BMU est sur I2C_NUM_1 (GPIO40 SCL / GPIO41 SDA, 50 kHz).
 */

#include "bmu_i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "I2C";
static SemaphoreHandle_t s_i2c_mutex = NULL;
static int s_consecutive_failures = 0;
#define BMU_I2C_RECOVERY_THRESHOLD 5

esp_err_t bmu_i2c_init(void)
{
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = BMU_I2C_SDA_GPIO;
    conf.scl_io_num = BMU_I2C_SCL_GPIO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE; /* External 4.7k on PCB */
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
    conf.master.clk_speed = BMU_I2C_FREQ_HZ;

    esp_err_t ret = i2c_param_config(BMU_I2C_PORT, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(BMU_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create I2C bus mutex for multi-register atomic operations (audit H-01) */
    s_i2c_mutex = xSemaphoreCreateMutex();
    configASSERT(s_i2c_mutex != NULL);

    ESP_LOGI(TAG, "BMU I2C init OK — SDA=%d SCL=%d %dHz port=%d",
             BMU_I2C_SDA_GPIO, BMU_I2C_SCL_GPIO, BMU_I2C_FREQ_HZ, BMU_I2C_PORT);
    return ESP_OK;
}

/* ── Low-level I2C helpers (no mutex — callers lock when needed) ────────── */

esp_err_t bmu_i2c_write_read(uint8_t addr, const uint8_t *write_buf, size_t write_len,
                              uint8_t *read_buf, size_t read_len, uint32_t timeout_ms)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    /* Write phase */
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (write_len > 0) {
        i2c_master_write(cmd, write_buf, write_len, true);
    }
    /* Read phase (repeated start) */
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (read_len > 1) {
        i2c_master_read(cmd, read_buf, read_len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, read_buf + read_len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(BMU_I2C_PORT, cmd, pdMS_TO_TICKS(timeout_ms));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t bmu_i2c_write(uint8_t addr, const uint8_t *buf, size_t len, uint32_t timeout_ms)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    if (len > 0) {
        i2c_master_write(cmd, buf, len, true);
    }
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(BMU_I2C_PORT, cmd, pdMS_TO_TICKS(timeout_ms));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* ── I2C scan ─────────────────────────────────────────────────────────────── */

/**
 * @brief Probe une adresse I2C (write-only, pas de data).
 * @return ESP_OK si le device repond ACK.
 */
static esp_err_t i2c_probe(uint8_t addr)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(BMU_I2C_PORT, cmd, pdMS_TO_TICKS(20));
    i2c_cmd_link_delete(cmd);
    return ret;
}

int bmu_i2c_scan(void)
{
    int count = 0;
    ESP_LOGI(TAG, "Scanning BMU I2C bus (0x20-0x4F)...");

    /* Scan seulement les plages utiles : TCA 0x20-0x27, INA 0x40-0x4F */
    const uint8_t ranges[][2] = { {0x20, 0x27}, {0x40, 0x4F} };
    for (int r = 0; r < 2; r++) {
        for (uint8_t addr = ranges[r][0]; addr <= ranges[r][1]; addr++) {
            if (i2c_probe(addr) != ESP_OK) {
                continue;
            }

            /* Identifier le device si possible */
            const char *type = "inconnu";

            /* Tenter lecture MANUFACTURER_ID INA237 (reg 0x3E) */
            uint8_t tx = 0x3E;
            uint8_t rx[2] = {0};
            if (bmu_i2c_write_read(addr, &tx, 1, rx, 2, 50) == ESP_OK) {
                uint16_t mfr = ((uint16_t)rx[0] << 8) | rx[1];
                if (mfr == 0x5449) {
                    /* Lire DEVICE_ID (reg 0x3F) */
                    tx = 0x3F;
                    if (bmu_i2c_write_read(addr, &tx, 1, rx, 2, 50) == ESP_OK) {
                        uint16_t did = ((uint16_t)rx[0] << 8) | rx[1];
                        ESP_LOGI(TAG, "  Found 0x%02X — TI INA237 (MFR=0x%04X DEV=0x%04X)", addr, mfr, did);
                        type = "INA237";
                    }
                } else if (addr >= 0x20 && addr <= 0x27) {
                    /* TCA9535 n'a pas de registre ID — identifier par plage d'adresses */
                    tx = 0x00;
                    if (bmu_i2c_write_read(addr, &tx, 1, rx, 1, 50) == ESP_OK) {
                        ESP_LOGI(TAG, "  Found 0x%02X — TCA9535 (Port0=0x%02X)", addr, rx[0]);
                        type = "TCA9535";
                    }
                }
            }
            if (type[0] == 'i') { /* "inconnu" */
                ESP_LOGI(TAG, "  Found 0x%02X — %s", addr, type);
            }
            count++;
        }
    } /* for ranges */

    ESP_LOGI(TAG, "Scan complete: %d device(s)", count);
    return count;
}

/* ── Mutex ─────────────────────────────────────────────────────────────────── */

esp_err_t bmu_i2c_lock(void)
{
    if (s_i2c_mutex == NULL) return ESP_ERR_INVALID_STATE;
    return (xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
           ? ESP_OK : ESP_ERR_TIMEOUT;
}

void bmu_i2c_unlock(void)
{
    if (s_i2c_mutex != NULL) xSemaphoreGive(s_i2c_mutex);
}

/* ── Recovery ──────────────────────────────────────────────────────────────── */

void bmu_i2c_record_success(void)
{
    s_consecutive_failures = 0;
}

void bmu_i2c_record_failure(void)
{
    s_consecutive_failures++;
    if (s_consecutive_failures >= BMU_I2C_RECOVERY_THRESHOLD) {
        ESP_LOGW(TAG, "I2C: %d echecs consecutifs — tentative de recovery bus",
                 s_consecutive_failures);
        i2c_reset_tx_fifo(BMU_I2C_PORT);
        i2c_reset_rx_fifo(BMU_I2C_PORT);
        ESP_LOGI(TAG, "I2C bus FIFO reset done");
        s_consecutive_failures = 0;
    }
}
