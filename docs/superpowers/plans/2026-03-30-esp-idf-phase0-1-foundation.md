# ESP-IDF Migration Phase 0+1: Foundation + I2C Drivers

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a minimal ESP-IDF v5.3 project that boots on ESP32-S3-BOX-3, initializes a dedicated I2C bus on PMOD1 (GPIO40/41), scans for INA237 and TCA9535 devices, reads voltage/current, and controls battery switches + LEDs.

**Architecture:** ESP-IDF CMake project with 3 custom components (`bmu_i2c`, `bmu_ina237`, `bmu_tca9535`). Uses the new `i2c_master` driver (ESP-IDF v5.x) which is thread-safe by design — no manual mutex needed. INA237 and TCA9535 drivers are written from scratch against TI datasheets, not ported from Arduino libs. All logging via `esp_log`.

**Tech Stack:** ESP-IDF v5.3 LTS, CMake, C++ (C-compatible interfaces), Unity test framework

**Spec:** `docs/superpowers/specs/2026-03-30-esp-idf-migration-design.md`

---

## Scope Note

This plan covers **Phase 0 (scaffold) and Phase 1 (I2C drivers)** only. Subsequent phases will be separate plans:
- Phase 2: Protection logic (`bmu_protection`, `bmu_battery_manager`, `bmu_config`)
- Phase 3: Web + Storage (`bmu_web`, `bmu_storage`)
- Phase 4: Cloud (`bmu_cloud`)
- Phase 5: Tests + CI + OTA

## File Structure

```
firmware-idf/                           # New directory (parallel to firmware/)
├── CMakeLists.txt                      # Top-level: cmake_minimum_required + project()
├── sdkconfig.defaults                  # ESP32-S3 flash/PSRAM/log config
├── sdkconfig.defaults.esp32s3          # Target-specific overrides
├── partitions.csv                      # 16MB flash partition table
├── main/
│   ├── CMakeLists.txt                  # idf_component_register(SRCS main.cpp)
│   └── main.cpp                        # app_main() — I2C init + scan + read loop
├── components/
│   ├── bmu_i2c/
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_i2c.h           # Bus init + device add API
│   │   └── bmu_i2c.cpp                 # I2C_NUM_1 on GPIO40/41 @ 50kHz
│   ├── bmu_ina237/
│   │   ├── CMakeLists.txt
│   │   ├── include/bmu_ina237.h        # INA237 driver API (init, read V/I/P)
│   │   └── bmu_ina237.cpp              # Register-level I2C operations
│   └── bmu_tca9535/
│       ├── CMakeLists.txt
│       ├── include/bmu_tca9535.h       # TCA9535 driver API (init, read/write pin)
│       └── bmu_tca9535.cpp             # Register-level I2C operations
└── test/
    └── test_i2c_scan/
        └── test_i2c_scan.cpp           # On-target test: scan + read
```

---

### Task 1: ESP-IDF Project Scaffold

**Files:**
- Create: `firmware-idf/CMakeLists.txt`
- Create: `firmware-idf/sdkconfig.defaults`
- Create: `firmware-idf/partitions.csv`
- Create: `firmware-idf/main/CMakeLists.txt`
- Create: `firmware-idf/main/main.cpp`

- [ ] **Step 1: Create top-level CMakeLists.txt**

```cmake
# firmware-idf/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(kxkm-bmu)
```

- [ ] **Step 2: Create sdkconfig.defaults**

```ini
# firmware-idf/sdkconfig.defaults
# ── Target ────────────────────────────────────────────────────────────
CONFIG_IDF_TARGET="esp32s3"

# ── Flash ─────────────────────────────────────────────────────────────
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# ── PSRAM (N16R8 = 8MB Octal PSRAM) ──────────────────────────────────
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y

# ── Logging ───────────────────────────────────────────────────────────
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_MAXIMUM_LEVEL_DEBUG=y

# ── FreeRTOS ──────────────────────────────────────────────────────────
CONFIG_FREERTOS_HZ=1000

# ── Watchdog ──────────────────────────────────────────────────────────
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
```

- [ ] **Step 3: Create partitions.csv**

```csv
# Name,   Type, SubType, Offset,   Size,    Flags
nvs,      data, nvs,     0x9000,   0x6000,
phy_init, data, phy,     0xf000,   0x1000,
factory,  app,  factory, 0x10000,  0x300000,
storage,  data, spiffs,  0x310000, 0x100000,
fatfs,    data, fat,     0x410000, 0xBF0000,
```

- [ ] **Step 4: Create main/CMakeLists.txt**

```cmake
# firmware-idf/main/CMakeLists.txt
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES bmu_i2c bmu_ina237 bmu_tca9535
)
```

- [ ] **Step 5: Create minimal main.cpp**

```cpp
// firmware-idf/main/main.cpp
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "KXKM BMU starting — ESP-IDF v5.3");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    while (true) {
        ESP_LOGI(TAG, "BMU loop — heap: %lu", (unsigned long)esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 6: Verify build**

```bash
cd firmware-idf
idf.py set-target esp32s3
idf.py build
```

Expected: BUILD SUCCESS. Binary in `build/kxkm-bmu.bin`.

- [ ] **Step 7: Flash and verify boot**

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Expected: Logs show `KXKM BMU starting — ESP-IDF v5.3` + free heap ~300KB+.

- [ ] **Step 8: Commit**

```bash
git add firmware-idf/
git commit -m "feat(idf): scaffold ESP-IDF v5.3 project for ESP32-S3-BOX-3 (Phase 0)"
```

---

### Task 2: bmu_i2c Component — Dedicated BMU I2C Bus

**Files:**
- Create: `firmware-idf/components/bmu_i2c/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_i2c/include/bmu_i2c.h`
- Create: `firmware-idf/components/bmu_i2c/bmu_i2c.cpp`
- Modify: `firmware-idf/main/main.cpp`

- [ ] **Step 1: Create component CMakeLists.txt**

```cmake
# firmware-idf/components/bmu_i2c/CMakeLists.txt
idf_component_register(
    SRCS "bmu_i2c.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver
)
```

- [ ] **Step 2: Create bmu_i2c.h**

```cpp
// firmware-idf/components/bmu_i2c/include/bmu_i2c.h
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// BMU I2C bus on PMOD1: SDA=GPIO41, SCL=GPIO40, I2C_NUM_1, 50kHz
// Pull-ups 4.7kΩ externes requises (non montées sur dock BOX-3)

#define BMU_I2C_SDA_GPIO    GPIO_NUM_41
#define BMU_I2C_SCL_GPIO    GPIO_NUM_40
#define BMU_I2C_PORT        I2C_NUM_1
#define BMU_I2C_FREQ_HZ     50000

/**
 * @brief Initialize the BMU-dedicated I2C bus.
 * @param[out] bus_handle  Returned bus handle.
 * @return ESP_OK on success.
 */
esp_err_t bmu_i2c_init(i2c_master_bus_handle_t *bus_handle);

/**
 * @brief Add a device on the BMU I2C bus.
 * @param bus       Bus handle from bmu_i2c_init().
 * @param addr      7-bit I2C address (e.g. 0x40 for INA237).
 * @param[out] dev  Returned device handle.
 * @return ESP_OK on success.
 */
esp_err_t bmu_i2c_add_device(i2c_master_bus_handle_t bus, uint8_t addr,
                              i2c_master_dev_handle_t *dev);

/**
 * @brief Scan the BMU I2C bus and log all detected devices.
 * @param bus  Bus handle.
 * @return Number of devices found.
 */
int bmu_i2c_scan(i2c_master_bus_handle_t bus);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Create bmu_i2c.cpp**

```cpp
// firmware-idf/components/bmu_i2c/bmu_i2c.cpp
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
    bus_config.flags.enable_internal_pullup = false; // External 4.7kΩ required

    esp_err_t ret = i2c_new_master_bus(&bus_config, bus_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "BMU I2C bus init OK — SDA=%d SCL=%d freq=%dHz port=%d",
                 BMU_I2C_SDA_GPIO, BMU_I2C_SCL_GPIO, BMU_I2C_FREQ_HZ, BMU_I2C_PORT);
    } else {
        ESP_LOGE(TAG, "BMU I2C bus init FAILED: %s", esp_err_to_name(ret));
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

    esp_err_t ret = i2c_master_bus_add_device(bus, &dev_config, dev);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Added device 0x%02X", addr);
    } else {
        ESP_LOGW(TAG, "Failed to add device 0x%02X: %s", addr, esp_err_to_name(ret));
    }
    return ret;
}

int bmu_i2c_scan(i2c_master_bus_handle_t bus)
{
    int count = 0;
    ESP_LOGI(TAG, "Scanning BMU I2C bus...");

    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        esp_err_t ret = i2c_master_probe(bus, addr, pdMS_TO_TICKS(50));
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  Found device at 0x%02X (%d)", addr, addr);
            count++;
        }
    }

    ESP_LOGI(TAG, "Scan complete: %d device(s) found", count);
    return count;
}
```

- [ ] **Step 4: Update main.cpp to use bmu_i2c**

```cpp
// firmware-idf/main/main.cpp
#include "bmu_i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "KXKM BMU starting — ESP-IDF v5.3");

    // Init BMU I2C bus on PMOD1 (GPIO40/41, 50kHz)
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(bmu_i2c_init(&i2c_bus));

    // Scan for INA237 (0x40-0x4F) and TCA9535 (0x20-0x27)
    int device_count = bmu_i2c_scan(i2c_bus);
    ESP_LOGI(TAG, "Total I2C devices: %d", device_count);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

- [ ] **Step 5: Build and verify I2C scan**

```bash
cd firmware-idf
idf.py build && idf.py -p /dev/ttyACM0 flash monitor
```

Expected: Logs show `Found device at 0x20`, `0x40`, etc. (or `0 device(s)` if no hardware connected — that's OK, the bus init must succeed).

- [ ] **Step 6: Commit**

```bash
git add firmware-idf/components/bmu_i2c/ firmware-idf/main/main.cpp
git commit -m "feat(idf): add bmu_i2c component — dedicated I2C bus on PMOD1 GPIO40/41"
```

---

### Task 3: bmu_ina237 Component — INA237 Voltage/Current Driver

**Files:**
- Create: `firmware-idf/components/bmu_ina237/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_ina237/include/bmu_ina237.h`
- Create: `firmware-idf/components/bmu_ina237/bmu_ina237.cpp`
- Modify: `firmware-idf/main/main.cpp`

- [ ] **Step 1: Create component CMakeLists.txt**

```cmake
# firmware-idf/components/bmu_ina237/CMakeLists.txt
idf_component_register(
    SRCS "bmu_ina237.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver bmu_i2c
)
```

- [ ] **Step 2: Create bmu_ina237.h**

```cpp
// firmware-idf/components/bmu_ina237/include/bmu_ina237.h
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// INA237 I2C addresses: 0x40-0x4F (16 max)
#define INA237_ADDR_BASE     0x40
#define INA237_ADDR_MAX      0x4F
#define INA237_MAX_DEVICES   16

// INA237 registers (from TI SBOS945 datasheet)
#define INA237_REG_CONFIG        0x00
#define INA237_REG_ADC_CONFIG    0x01
#define INA237_REG_SHUNT_CAL     0x02
#define INA237_REG_SHUNT_VOLTAGE 0x04
#define INA237_REG_BUS_VOLTAGE   0x05
#define INA237_REG_DIAG_ALERT    0x0B
#define INA237_REG_SHUNT_OV_TH   0x0C
#define INA237_REG_SHUNT_UV_TH   0x0D
#define INA237_REG_BUS_OV_TH     0x0E
#define INA237_REG_BUS_UV_TH     0x0F
#define INA237_REG_TEMP          0x06
#define INA237_REG_CURRENT       0x07
#define INA237_REG_POWER         0x08
#define INA237_REG_MANUFACTURER  0x3E
#define INA237_REG_DEVICE_ID     0x3F

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t addr;
    float current_lsb;  // A/bit, computed from calibration
    float shunt_ohm;
    float max_current_a;
} bmu_ina237_t;

/**
 * @brief Initialize one INA237 device.
 * @param bus           I2C bus handle.
 * @param addr          7-bit address (0x40-0x4F).
 * @param shunt_ohm     Shunt resistance in ohms (e.g. 0.002 for 2mΩ).
 * @param max_current_a Maximum expected current in amps.
 * @param[out] handle   Returned handle.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if device not detected.
 */
esp_err_t bmu_ina237_init(i2c_master_bus_handle_t bus, uint8_t addr,
                           float shunt_ohm, float max_current_a,
                           bmu_ina237_t *handle);

/**
 * @brief Read bus voltage.
 * @return ESP_OK on success. voltage_v set to volts.
 */
esp_err_t bmu_ina237_read_voltage(bmu_ina237_t *h, float *voltage_v);

/**
 * @brief Read current.
 * @return ESP_OK on success. current_a set to amps (positive = discharge).
 */
esp_err_t bmu_ina237_read_current(bmu_ina237_t *h, float *current_a);

/**
 * @brief Read voltage and current atomically (single bus transaction window).
 */
esp_err_t bmu_ina237_read_all(bmu_ina237_t *h, float *voltage_v,
                               float *current_a, float *power_w);

/**
 * @brief Scan bus for all INA237 devices (0x40-0x4F).
 * @param bus       I2C bus handle.
 * @param devices   Array of INA237_MAX_DEVICES handles to populate.
 * @param shunt_ohm Shunt resistance for all devices.
 * @param max_current_a Max current for all devices.
 * @return Number of INA237 devices found and initialized.
 */
int bmu_ina237_scan_init(i2c_master_bus_handle_t bus,
                          bmu_ina237_t devices[INA237_MAX_DEVICES],
                          float shunt_ohm, float max_current_a);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Create bmu_ina237.cpp**

```cpp
// firmware-idf/components/bmu_ina237/bmu_ina237.cpp
#include "bmu_ina237.h"
#include "bmu_i2c.h"
#include "esp_log.h"
#include <cmath>
#include <cstring>

static const char *TAG = "INA";

// INA237 bus voltage LSB = 3.125 mV/bit (SBOS945 Table 7-6)
static const float BUS_VOLTAGE_LSB = 3.125e-3f;

// Write a 16-bit register
static esp_err_t ina237_write_reg(i2c_master_dev_handle_t dev,
                                   uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = { reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    return i2c_master_transmit(dev, buf, 3, pdMS_TO_TICKS(50));
}

// Read a 16-bit register
static esp_err_t ina237_read_reg(i2c_master_dev_handle_t dev,
                                  uint8_t reg, uint16_t *val)
{
    uint8_t tx = reg;
    uint8_t rx[2] = {0};
    esp_err_t ret = i2c_master_transmit_receive(dev, &tx, 1, rx, 2, pdMS_TO_TICKS(50));
    if (ret == ESP_OK) {
        *val = ((uint16_t)rx[0] << 8) | rx[1];
    }
    return ret;
}

esp_err_t bmu_ina237_init(i2c_master_bus_handle_t bus, uint8_t addr,
                           float shunt_ohm, float max_current_a,
                           bmu_ina237_t *handle)
{
    memset(handle, 0, sizeof(*handle));
    handle->addr = addr;
    handle->shunt_ohm = shunt_ohm;
    handle->max_current_a = max_current_a;

    // Add device to bus
    esp_err_t ret = bmu_i2c_add_device(bus, addr, &handle->dev);
    if (ret != ESP_OK) {
        return ret;
    }

    // Verify manufacturer ID (should be 0x5449 = "TI")
    uint16_t mfr_id = 0;
    ret = ina237_read_reg(handle->dev, INA237_REG_MANUFACTURER, &mfr_id);
    if (ret != ESP_OK || mfr_id != 0x5449) {
        ESP_LOGW(TAG, "INA237 0x%02X: unexpected manufacturer ID 0x%04X", addr, mfr_id);
        // Continue anyway — some clones have different IDs
    }

    // Reset device
    ret = ina237_write_reg(handle->dev, INA237_REG_CONFIG, 0x8000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "INA237 0x%02X: reset failed", addr);
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(2)); // Wait for reset

    // Calculate current LSB: current_lsb = max_current / 2^15
    handle->current_lsb = max_current_a / 32768.0f;

    // Calculate calibration register: SHUNT_CAL = 819.2e6 * current_lsb * shunt_ohm
    // (SBOS945 section 7.5.1, ADCRANGE=0)
    float cal_f = 819.2e6f * handle->current_lsb * shunt_ohm;
    uint16_t cal = (uint16_t)(cal_f + 0.5f);
    if (cal == 0) cal = 1;

    ret = ina237_write_reg(handle->dev, INA237_REG_SHUNT_CAL, cal);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "INA237 0x%02X: calibration write failed", addr);
        return ret;
    }

    // Set ADC config: continuous bus+shunt, 1024 averages, 1052µs conversion
    // ADC_CONFIG = 0b_0_100_100_100_100_000 = 0x4920
    // Bits 15: reserved=0
    // Bits 14-12: MODE=100 (continuous bus+shunt)  ... wait, INA237 mode is in CONFIG not ADC_CONFIG
    // INA237 CONFIG register (0x00): bits 15-12 = RST(0) + RSTACC(0) + CONVDLY(0) + ADCRANGE(0)
    // INA237 ADC_CONFIG (0x01): bits 15-12=MODE, 11-9=VBUSCT, 8-6=VSHCT, 5-3=VTCT, 2-0=AVG
    // MODE = 1111 (continuous temp+bus+shunt) = 0xF
    // VBUSCT = 100 (1052µs) = 0x4
    // VSHCT  = 100 (1052µs) = 0x4
    // VTCT   = 100 (1052µs) = 0x4
    // AVG    = 011 (64 averages)  = 0x3
    uint16_t adc_config = (0xF << 12) | (0x4 << 9) | (0x4 << 6) | (0x4 << 3) | 0x3;
    ret = ina237_write_reg(handle->dev, INA237_REG_ADC_CONFIG, adc_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "INA237 0x%02X: ADC config write failed", addr);
        return ret;
    }

    ESP_LOGI(TAG, "INA237 0x%02X init OK — shunt=%.4fΩ max=%.1fA cal=%u lsb=%.6fA",
             addr, shunt_ohm, max_current_a, cal, handle->current_lsb);
    return ESP_OK;
}

esp_err_t bmu_ina237_read_voltage(bmu_ina237_t *h, float *voltage_v)
{
    uint16_t raw = 0;
    esp_err_t ret = ina237_read_reg(h->dev, INA237_REG_BUS_VOLTAGE, &raw);
    if (ret != ESP_OK) {
        *voltage_v = NAN;
        return ret;
    }
    // Bus voltage register: 16-bit unsigned, LSB = 3.125 mV
    *voltage_v = (float)raw * BUS_VOLTAGE_LSB;
    return ESP_OK;
}

esp_err_t bmu_ina237_read_current(bmu_ina237_t *h, float *current_a)
{
    uint16_t raw = 0;
    esp_err_t ret = ina237_read_reg(h->dev, INA237_REG_CURRENT, &raw);
    if (ret != ESP_OK) {
        *current_a = NAN;
        return ret;
    }
    // Current register: 16-bit signed (two's complement), LSB = current_lsb
    *current_a = (float)(int16_t)raw * h->current_lsb;
    return ESP_OK;
}

esp_err_t bmu_ina237_read_all(bmu_ina237_t *h, float *voltage_v,
                               float *current_a, float *power_w)
{
    esp_err_t ret;

    ret = bmu_ina237_read_voltage(h, voltage_v);
    if (ret != ESP_OK) return ret;

    ret = bmu_ina237_read_current(h, current_a);
    if (ret != ESP_OK) return ret;

    if (power_w != NULL) {
        uint16_t raw = 0;
        ret = ina237_read_reg(h->dev, INA237_REG_POWER, &raw);
        if (ret != ESP_OK) {
            *power_w = NAN;
            return ret;
        }
        // Power register LSB = 0.2 * current_lsb (SBOS945 section 7.5.3)
        *power_w = (float)raw * 0.2f * h->current_lsb;
    }

    return ESP_OK;
}

int bmu_ina237_scan_init(i2c_master_bus_handle_t bus,
                          bmu_ina237_t devices[INA237_MAX_DEVICES],
                          float shunt_ohm, float max_current_a)
{
    int count = 0;
    for (uint8_t addr = INA237_ADDR_BASE; addr <= INA237_ADDR_MAX && count < INA237_MAX_DEVICES; addr++) {
        // Probe first to avoid noisy error logs
        if (i2c_master_probe(bus, addr, pdMS_TO_TICKS(50)) != ESP_OK) {
            continue;
        }
        esp_err_t ret = bmu_ina237_init(bus, addr, shunt_ohm, max_current_a, &devices[count]);
        if (ret == ESP_OK) {
            count++;
        }
    }
    ESP_LOGI(TAG, "INA237 scan: %d device(s) initialized", count);
    return count;
}
```

- [ ] **Step 4: Update main.cpp to scan and read INA237**

```cpp
// firmware-idf/main/main.cpp
#include "bmu_i2c.h"
#include "bmu_ina237.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "KXKM BMU starting — ESP-IDF v5.3");

    // Init BMU I2C bus on PMOD1 (GPIO40/41, 50kHz)
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(bmu_i2c_init(&i2c_bus));

    // Scan and init all INA237 devices (shunt=2mΩ, max=0.5A)
    bmu_ina237_t ina_devices[INA237_MAX_DEVICES] = {};
    int nb_ina = bmu_ina237_scan_init(i2c_bus, ina_devices, 0.002f, 0.5f);
    ESP_LOGI(TAG, "Found %d INA237 device(s)", nb_ina);

    // Main loop: read voltage/current from all INA237
    while (true) {
        for (int i = 0; i < nb_ina; i++) {
            float v = 0, c = 0;
            esp_err_t ret = bmu_ina237_read_all(&ina_devices[i], &v, &c, NULL);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "INA[%d] 0x%02X: V=%.3fV I=%.3fA",
                         i, ina_devices[i].addr, v, c);
            } else {
                ESP_LOGW(TAG, "INA[%d] 0x%02X: read error %s",
                         i, ina_devices[i].addr, esp_err_to_name(ret));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

- [ ] **Step 5: Build and verify on hardware**

```bash
idf.py build && idf.py -p /dev/ttyACM0 flash monitor
```

Expected: With INA237 hardware connected, logs show voltage/current readings. Without hardware: `INA237 scan: 0 device(s)`.

- [ ] **Step 6: Commit**

```bash
git add firmware-idf/components/bmu_ina237/ firmware-idf/main/main.cpp
git commit -m "feat(idf): add bmu_ina237 driver — register-level INA237 V/I/P reads"
```

---

### Task 4: bmu_tca9535 Component — GPIO Expander Driver

**Files:**
- Create: `firmware-idf/components/bmu_tca9535/CMakeLists.txt`
- Create: `firmware-idf/components/bmu_tca9535/include/bmu_tca9535.h`
- Create: `firmware-idf/components/bmu_tca9535/bmu_tca9535.cpp`
- Modify: `firmware-idf/main/main.cpp`

- [ ] **Step 1: Create component CMakeLists.txt**

```cmake
# firmware-idf/components/bmu_tca9535/CMakeLists.txt
idf_component_register(
    SRCS "bmu_tca9535.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver bmu_i2c
)
```

- [ ] **Step 2: Create bmu_tca9535.h**

```cpp
// firmware-idf/components/bmu_tca9535/include/bmu_tca9535.h
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// TCA9535 I2C addresses: 0x20-0x27 (8 max)
#define TCA9535_ADDR_BASE     0x20
#define TCA9535_ADDR_MAX      0x27
#define TCA9535_MAX_DEVICES   8

// TCA9535 registers
#define TCA9535_REG_INPUT_0    0x00
#define TCA9535_REG_INPUT_1    0x01
#define TCA9535_REG_OUTPUT_0   0x02
#define TCA9535_REG_OUTPUT_1   0x03
#define TCA9535_REG_POLARITY_0 0x04
#define TCA9535_REG_POLARITY_1 0x05
#define TCA9535_REG_CONFIG_0   0x06  // 0=output, 1=input
#define TCA9535_REG_CONFIG_1   0x07

// BMU pin mapping per TCA9535:
// P0.0-P0.3 = battery switches (output, active high)
// P0.4-P0.7 = alert inputs (input)
// P1.0-P1.7 = LED pairs: P1.0=red1, P1.1=green1, P1.2=red2, etc.

typedef struct {
    i2c_master_dev_handle_t dev;
    uint8_t addr;
    uint8_t output_state[2]; // Cached output register state
} bmu_tca9535_t;

esp_err_t bmu_tca9535_init(i2c_master_bus_handle_t bus, uint8_t addr,
                            bmu_tca9535_t *handle);

/**
 * @brief Write a single pin.
 * @param pin 0-15 (0-7 = port0, 8-15 = port1).
 */
esp_err_t bmu_tca9535_write_pin(bmu_tca9535_t *h, uint8_t pin, bool value);

/**
 * @brief Read a single pin.
 */
esp_err_t bmu_tca9535_read_pin(bmu_tca9535_t *h, uint8_t pin, bool *value);

/**
 * @brief Scan bus for all TCA9535 devices.
 * @return Number of TCA9535 devices found and initialized.
 */
int bmu_tca9535_scan_init(i2c_master_bus_handle_t bus,
                           bmu_tca9535_t devices[TCA9535_MAX_DEVICES]);

/**
 * @brief Set battery switch state.
 * @param tca_idx  Index into devices array (0-7).
 * @param channel  Battery channel on this TCA (0-3).
 * @param on       true=switch ON, false=switch OFF.
 */
esp_err_t bmu_tca9535_switch_battery(bmu_tca9535_t *h, uint8_t channel, bool on);

/**
 * @brief Set LED state for a battery channel.
 * @param channel 0-3 within this TCA.
 * @param red     true=red ON.
 * @param green   true=green ON.
 */
esp_err_t bmu_tca9535_set_led(bmu_tca9535_t *h, uint8_t channel,
                               bool red, bool green);

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 3: Create bmu_tca9535.cpp**

```cpp
// firmware-idf/components/bmu_tca9535/bmu_tca9535.cpp
#include "bmu_tca9535.h"
#include "bmu_i2c.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "TCA";

static esp_err_t tca_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(dev, buf, 2, pdMS_TO_TICKS(50));
}

static esp_err_t tca_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    uint8_t tx = reg;
    return i2c_master_transmit_receive(dev, &tx, 1, val, 1, pdMS_TO_TICKS(50));
}

esp_err_t bmu_tca9535_init(i2c_master_bus_handle_t bus, uint8_t addr,
                            bmu_tca9535_t *handle)
{
    memset(handle, 0, sizeof(*handle));
    handle->addr = addr;

    esp_err_t ret = bmu_i2c_add_device(bus, addr, &handle->dev);
    if (ret != ESP_OK) return ret;

    // Configure port 0: pins 0-3 = output (switches), pins 4-7 = input (alerts)
    // Config register: 0=output, 1=input → 0xF0
    ret = tca_write_reg(handle->dev, TCA9535_REG_CONFIG_0, 0xF0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA 0x%02X: port0 config failed", addr);
        return ret;
    }

    // Configure port 1: pins 8-15 = all output (LEDs) → 0x00
    ret = tca_write_reg(handle->dev, TCA9535_REG_CONFIG_1, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TCA 0x%02X: port1 config failed", addr);
        return ret;
    }

    // Set all outputs LOW (switches OFF, LEDs OFF)
    handle->output_state[0] = 0x00;
    handle->output_state[1] = 0x00;
    tca_write_reg(handle->dev, TCA9535_REG_OUTPUT_0, 0x00);
    tca_write_reg(handle->dev, TCA9535_REG_OUTPUT_1, 0x00);

    ESP_LOGI(TAG, "TCA9535 0x%02X init OK", addr);
    return ESP_OK;
}

esp_err_t bmu_tca9535_write_pin(bmu_tca9535_t *h, uint8_t pin, bool value)
{
    if (pin > 15) return ESP_ERR_INVALID_ARG;

    uint8_t port = (pin < 8) ? 0 : 1;
    uint8_t bit = pin % 8;
    uint8_t reg = (port == 0) ? TCA9535_REG_OUTPUT_0 : TCA9535_REG_OUTPUT_1;

    if (value) {
        h->output_state[port] |= (1 << bit);
    } else {
        h->output_state[port] &= ~(1 << bit);
    }

    return tca_write_reg(h->dev, reg, h->output_state[port]);
}

esp_err_t bmu_tca9535_read_pin(bmu_tca9535_t *h, uint8_t pin, bool *value)
{
    if (pin > 15) return ESP_ERR_INVALID_ARG;

    uint8_t port = (pin < 8) ? 0 : 1;
    uint8_t bit = pin % 8;
    uint8_t reg = (port == 0) ? TCA9535_REG_INPUT_0 : TCA9535_REG_INPUT_1;

    uint8_t data = 0;
    esp_err_t ret = tca_read_reg(h->dev, reg, &data);
    if (ret != ESP_OK) return ret;

    *value = (data >> bit) & 1;
    return ESP_OK;
}

int bmu_tca9535_scan_init(i2c_master_bus_handle_t bus,
                           bmu_tca9535_t devices[TCA9535_MAX_DEVICES])
{
    int count = 0;
    for (uint8_t addr = TCA9535_ADDR_BASE; addr <= TCA9535_ADDR_MAX && count < TCA9535_MAX_DEVICES; addr++) {
        if (i2c_master_probe(bus, addr, pdMS_TO_TICKS(50)) != ESP_OK) {
            continue;
        }
        esp_err_t ret = bmu_tca9535_init(bus, addr, &devices[count]);
        if (ret == ESP_OK) {
            count++;
        }
    }
    ESP_LOGI(TAG, "TCA9535 scan: %d device(s) initialized", count);
    return count;
}

esp_err_t bmu_tca9535_switch_battery(bmu_tca9535_t *h, uint8_t channel, bool on)
{
    if (channel > 3) return ESP_ERR_INVALID_ARG;
    // Pins 0-3 = battery switches
    return bmu_tca9535_write_pin(h, channel, on);
}

esp_err_t bmu_tca9535_set_led(bmu_tca9535_t *h, uint8_t channel,
                               bool red, bool green)
{
    if (channel > 3) return ESP_ERR_INVALID_ARG;
    // Pin mapping: red = pin (channel*2 + 8), green = pin (channel*2 + 9)
    esp_err_t ret = bmu_tca9535_write_pin(h, channel * 2 + 8, red);
    if (ret != ESP_OK) return ret;
    return bmu_tca9535_write_pin(h, channel * 2 + 9, green);
}
```

- [ ] **Step 4: Update main.cpp — full I2C scan + topology validation**

```cpp
// firmware-idf/main/main.cpp
#include "bmu_i2c.h"
#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>

static const char *TAG = "MAIN";

// Config (will move to bmu_config in Phase 2)
static const float SHUNT_OHM = 0.002f;     // 2mΩ shunt
static const float MAX_CURRENT_A = 0.5f;    // INA237 max current range

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "KXKM BMU starting — ESP-IDF v5.3");
    ESP_LOGI(TAG, "Free heap: %lu bytes", (unsigned long)esp_get_free_heap_size());

    // Init BMU I2C bus on PMOD1 (GPIO40/41, 50kHz)
    i2c_master_bus_handle_t i2c_bus = NULL;
    ESP_ERROR_CHECK(bmu_i2c_init(&i2c_bus));

    // Scan and init all devices
    bmu_ina237_t ina_devices[INA237_MAX_DEVICES] = {};
    int nb_ina = bmu_ina237_scan_init(i2c_bus, ina_devices, SHUNT_OHM, MAX_CURRENT_A);

    bmu_tca9535_t tca_devices[TCA9535_MAX_DEVICES] = {};
    int nb_tca = bmu_tca9535_scan_init(i2c_bus, tca_devices);

    ESP_LOGI(TAG, "Topology: %d INA237, %d TCA9535", nb_ina, nb_tca);

    // Topology validation: Nb_TCA * 4 == Nb_INA
    bool topology_valid = (nb_ina > 0) && (nb_tca > 0) && (nb_tca * 4 == nb_ina);
    if (!topology_valid) {
        ESP_LOGE(TAG, "TOPOLOGY MISMATCH: expected Nb_TCA*4 == Nb_INA (%d*4 != %d)",
                 nb_tca, nb_ina);
        ESP_LOGE(TAG, "FAIL-SAFE: all batteries will be forced OFF");
    }

    // Main loop
    while (true) {
        if (!topology_valid) {
            // Fail-safe: force all switches OFF
            for (int t = 0; t < nb_tca; t++) {
                for (int ch = 0; ch < 4; ch++) {
                    bmu_tca9535_switch_battery(&tca_devices[t], ch, false);
                    bmu_tca9535_set_led(&tca_devices[t], ch, true, false); // Red=ON
                }
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        // Read all batteries
        for (int i = 0; i < nb_ina; i++) {
            float v = 0, c = 0;
            esp_err_t ret = bmu_ina237_read_all(&ina_devices[i], &v, &c, NULL);
            if (ret == ESP_OK && !isnan(v)) {
                ESP_LOGI(TAG, "BAT[%d] V=%.3fV I=%.3fA", i + 1, v, c);
            } else {
                ESP_LOGW(TAG, "BAT[%d] read error", i + 1);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
```

- [ ] **Step 5: Build and verify**

```bash
idf.py build && idf.py -p /dev/ttyACM0 flash monitor
```

Expected: Boot logs show I2C scan results, topology validation, and voltage/current readings (or topology mismatch message if no hardware).

- [ ] **Step 6: Commit**

```bash
git add firmware-idf/components/bmu_tca9535/ firmware-idf/main/main.cpp
git commit -m "feat(idf): add bmu_tca9535 driver + topology validation in main loop"
```

---

### Task 5: Final Phase 0+1 Verification

- [ ] **Step 1: Clean build from scratch**

```bash
cd firmware-idf
rm -rf build
idf.py set-target esp32s3
idf.py build
```

Expected: BUILD SUCCESS, no warnings in bmu_* components.

- [ ] **Step 2: Check binary size**

```bash
idf.py size
```

Expected: App binary < 500 KB (Phase 0+1 is minimal — no WiFi, no web, no SD).

- [ ] **Step 3: Flash and verify with hardware (if available)**

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Verify:
- I2C scan finds INA237 and TCA9535 at expected addresses
- Topology validation passes (`Nb_TCA * 4 == Nb_INA`)
- Voltage readings are sane (24-30V range)
- Current readings are sane (near 0A with no load)

- [ ] **Step 4: Commit final state**

```bash
git add firmware-idf/
git commit -m "feat(idf): Phase 0+1 complete — ESP-IDF scaffold + I2C drivers (INA237/TCA9535)"
```
