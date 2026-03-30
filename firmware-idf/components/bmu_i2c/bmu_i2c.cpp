#include "bmu_i2c.h"
#include "esp_log.h"

static const char *TAG = "I2C";

esp_err_t bmu_i2c_init(i2c_master_bus_handle_t *bus_handle)
{
    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = BMU_I2C_PORT;
    bus_config.sda_io_num = BMU_I2C_SDA_GPIO;
    bus_config.scl_io_num = BMU_I2C_SCL_GPIO;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = false;

    esp_err_t ret = i2c_new_master_bus(&bus_config, bus_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "BMU I2C bus OK — SDA=%d SCL=%d %dHz port=%d",
                 BMU_I2C_SDA_GPIO, BMU_I2C_SCL_GPIO, BMU_I2C_FREQ_HZ, BMU_I2C_PORT);
    } else {
        ESP_LOGE(TAG, "BMU I2C bus FAILED: %s", esp_err_to_name(ret));
    }
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
