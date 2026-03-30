#include "bmu_i2c.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "I2C";
static SemaphoreHandle_t s_i2c_mutex = NULL;

esp_err_t bmu_i2c_init(i2c_master_bus_handle_t *bus_handle)
{
    /* Le bus DOCK BSP (I2C_NUM_1, GPIO40/41) est deja cree par bsp_i2c_init().
     * On recupere le handle existant plutot que de recreer le bus. */
    esp_err_t ret = i2c_master_get_bus_handle(BMU_I2C_PORT, bus_handle);
    if (ret == ESP_OK) {
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
    ESP_LOGI(TAG, "Scanning BMU I2C bus...");
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, pdMS_TO_TICKS(50)) == ESP_OK) {
            ESP_LOGI(TAG, "  Found 0x%02X", addr);
            count++;
        }
    }
    ESP_LOGI(TAG, "Scan: %d device(s)", count);
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
