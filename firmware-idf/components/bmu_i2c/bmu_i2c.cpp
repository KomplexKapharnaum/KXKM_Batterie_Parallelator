#include "bmu_i2c.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "I2C";
static SemaphoreHandle_t s_i2c_mutex = NULL;
static i2c_master_bus_handle_t s_bus_handle = NULL;
static int s_consecutive_failures = 0;
#define BMU_I2C_RECOVERY_THRESHOLD 5

esp_err_t bmu_i2c_init(i2c_master_bus_handle_t *bus_handle)
{
    /* Le bus DOCK BSP (I2C_NUM_1, GPIO40/41) est deja cree par bsp_i2c_init().
     * On recupere le handle existant plutot que de recreer le bus. */
    esp_err_t ret = i2c_master_get_bus_handle(BMU_I2C_PORT, bus_handle);
    if (ret == ESP_OK) {
        s_bus_handle = *bus_handle;
        ESP_LOGI(TAG, "BMU I2C bus OK — SDA=%d SCL=%d %dHz port=%d (BSP DOCK)",
                 BMU_I2C_SDA_GPIO, BMU_I2C_SCL_GPIO, BMU_I2C_FREQ_HZ, BMU_I2C_PORT);
    } else {
        ESP_LOGE(TAG, "BMU I2C get bus handle FAILED: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Create I2C bus mutex for multi-register atomic operations (audit H-01) */
    s_i2c_mutex = xSemaphoreCreateMutex();
    configASSERT(s_i2c_mutex != NULL);

    return ret;
}

esp_err_t bmu_i2c_add_device(i2c_master_bus_handle_t bus, uint8_t addr,
                              i2c_master_dev_handle_t *dev)
{
    i2c_device_config_t dev_config = {};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address = addr;
    dev_config.scl_speed_hz = BMU_I2C_FREQ_HZ;

    return i2c_master_bus_add_device(bus, &dev_config, dev);
}

int bmu_i2c_scan(i2c_master_bus_handle_t bus)
{
    int count = 0;
    ESP_LOGI(TAG, "Scanning BMU I2C bus (0x20-0x4F)...");

    /* Scan seulement les plages utiles : TCA 0x20-0x27, INA 0x40-0x4F */
    const uint8_t ranges[][2] = { {0x20, 0x27}, {0x40, 0x4F} };
    for (int r = 0; r < 2; r++) {
    for (uint8_t addr = ranges[r][0]; addr <= ranges[r][1]; addr++) {
        if (i2c_master_probe(bus, addr, pdMS_TO_TICKS(20)) != ESP_OK) {
            continue;
        }
        {
            /* Identifier le device si possible */
            i2c_master_dev_handle_t dev = NULL;
            i2c_device_config_t cfg = {};
            cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            cfg.device_address = addr;
            cfg.scl_speed_hz = BMU_I2C_FREQ_HZ;

            const char *type = "inconnu";
            if (i2c_master_bus_add_device(bus, &cfg, &dev) == ESP_OK) {
                /* Tenter lecture MANUFACTURER_ID INA237 (reg 0x3E) */
                uint8_t tx = 0x3E;
                uint8_t rx[2] = {0};
                if (i2c_master_transmit_receive(dev, &tx, 1, rx, 2, pdMS_TO_TICKS(50)) == ESP_OK) {
                    uint16_t mfr = ((uint16_t)rx[0] << 8) | rx[1];
                    if (mfr == 0x5449) {
                        /* Lire DEVICE_ID (reg 0x3F) */
                        tx = 0x3F;
                        if (i2c_master_transmit_receive(dev, &tx, 1, rx, 2, pdMS_TO_TICKS(50)) == ESP_OK) {
                            uint16_t did = ((uint16_t)rx[0] << 8) | rx[1];
                            ESP_LOGI(TAG, "  Found 0x%02X — TI INA237 (MFR=0x%04X DEV=0x%04X)", addr, mfr, did);
                            type = "INA237";
                        }
                    } else if (addr >= 0x20 && addr <= 0x27) {
                        /* TCA9535 n'a pas de registre ID — identifier par plage d'adresses */
                        /* Tenter lecture Input Port 0 (reg 0x00) */
                        tx = 0x00;
                        if (i2c_master_transmit_receive(dev, &tx, 1, rx, 1, pdMS_TO_TICKS(50)) == ESP_OK) {
                            ESP_LOGI(TAG, "  Found 0x%02X — TCA9535 (Port0=0x%02X)", addr, rx[0]);
                            type = "TCA9535";
                        }
                    }
                }
                if (type[0] == 'i') { /* "inconnu" */
                    ESP_LOGI(TAG, "  Found 0x%02X — %s", addr, type);
                }
                i2c_master_bus_rm_device(dev);
            } else {
                ESP_LOGI(TAG, "  Found 0x%02X (probe OK, add failed)", addr);
            }
            count++;
        }
    }
    } /* for ranges */

    ESP_LOGI(TAG, "Scan complete: %d device(s)", count);
    return count;
}

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
        if (s_bus_handle != NULL) {
            esp_err_t ret = i2c_master_bus_reset(s_bus_handle);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "I2C bus reset OK");
            } else {
                ESP_LOGE(TAG, "I2C bus reset FAILED: %s", esp_err_to_name(ret));
            }
        }
        s_consecutive_failures = 0;
    }
}
