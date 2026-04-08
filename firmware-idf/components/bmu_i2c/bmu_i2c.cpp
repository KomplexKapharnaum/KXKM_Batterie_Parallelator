#include "bmu_i2c.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
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

esp_err_t bmu_i2c_probe(i2c_master_bus_handle_t bus, uint8_t addr, TickType_t timeout_ticks)
{
    if (bmu_i2c_lock() != ESP_OK) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = i2c_master_probe(bus, addr, timeout_ticks);
    bmu_i2c_unlock();
    return ret;
}

int bmu_i2c_scan(i2c_master_bus_handle_t bus)
{
    int count = 0;
    ESP_LOGI(TAG, "Scanning BMU I2C bus (0x20-0x4F)...");

    /* Scan complet : add_device + read registres + rm_device.
     * Sert aussi de warm-up pour les esclaves derriere l'ISO1540. */
    const uint8_t ranges[][2] = { {0x20, 0x27}, {0x40, 0x4F} };
    for (int r = 0; r < 2; r++) {
        for (uint8_t addr = ranges[r][0]; addr <= ranges[r][1]; addr++) {
            i2c_master_dev_handle_t dev = NULL;
            i2c_device_config_t cfg = {};
            cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
            cfg.device_address = addr;
            cfg.scl_speed_hz = BMU_I2C_FREQ_HZ;

            if (i2c_master_bus_add_device(bus, &cfg, &dev) != ESP_OK) {
                continue;
            }

            /* Try a read to verify presence */
            uint8_t reg = (addr <= 0x27) ? 0x00 : 0x3E;
            uint8_t rx[2] = {0};
            esp_err_t ret = i2c_master_transmit_receive(dev, &reg, 1, rx,
                                (addr <= 0x27) ? 1 : 2, pdMS_TO_TICKS(50));

            if (ret == ESP_OK) {
                if (addr <= 0x27) {
                    ESP_LOGI(TAG, "  Found 0x%02X — TCA9535 (Port0=0x%02X)", addr, rx[0]);
                } else {
                    uint16_t mfr = ((uint16_t)rx[0] << 8) | rx[1];
                    ESP_LOGI(TAG, "  Found 0x%02X — INA237 (MFR=0x%04X)", addr, mfr);
                }
                count++;
            }

            i2c_master_bus_rm_device(dev);
        }
    }

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
        bmu_i2c_bus_recover();
        s_consecutive_failures = 0;
    }
}

esp_err_t bmu_i2c_bus_recover(void)
{
    const gpio_num_t sda = (gpio_num_t)BMU_I2C_SDA_GPIO;
    const gpio_num_t scl = (gpio_num_t)BMU_I2C_SCL_GPIO;

    /* Step 1: Try ESP-IDF built-in reset first */
    if (s_bus_handle != NULL) {
        esp_err_t ret = i2c_master_bus_reset(s_bus_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "I2C bus reset (ESP-IDF) OK");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "ESP-IDF bus reset failed: %s — trying bit-bang recovery",
                 esp_err_to_name(ret));
    }

    /* Step 2: Bit-bang 9 SCL clock pulses to free stuck SDA
     * Si un esclave maintient SDA low (ACK coincé), 9 clocks le font
     * revenir en idle car la trame I2C la plus longue = 9 bits. */
    ESP_LOGW(TAG, "I2C bit-bang recovery: 9 SCL pulses on GPIO%d/%d", scl, sda);

    /* Temporarily reconfigure SCL as GPIO output, SDA as GPIO input */
    gpio_config_t scl_cfg = {
        .pin_bit_mask = (1ULL << scl),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&scl_cfg);

    gpio_config_t sda_cfg = {
        .pin_bit_mask = (1ULL << sda),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&sda_cfg);

    /* 9 clock pulses — check if SDA releases after each */
    bool sda_freed = false;
    for (int i = 0; i < 9; i++) {
        gpio_set_level(scl, 0);
        ets_delay_us(5);
        gpio_set_level(scl, 1);
        ets_delay_us(5);

        if (gpio_get_level(sda) == 1) {
            sda_freed = true;
            ESP_LOGI(TAG, "SDA released after %d clock(s)", i + 1);
            break;
        }
    }

    /* Generate STOP condition: SDA low→high while SCL high */
    if (sda_freed) {
        /* SDA as output for STOP */
        gpio_set_direction(sda, GPIO_MODE_OUTPUT_OD);
        gpio_set_level(sda, 0);
        ets_delay_us(5);
        gpio_set_level(scl, 1);
        ets_delay_us(5);
        gpio_set_level(sda, 1);  /* STOP: SDA rises while SCL high */
        ets_delay_us(5);
    }

    /* Restore pins to I2C peripheral — reinit the bus handle */
    ESP_LOGI(TAG, "I2C recovery %s — bus will re-init on next operation",
             sda_freed ? "OK" : "FAILED (SDA still low)");

    return sda_freed ? ESP_OK : ESP_FAIL;
}
