/**
 * @file bmu_ina237.cpp
 * @brief Driver INA237 pour BMU — registre-level, conforme TI SBOS945.
 *
 * Toutes les operations I2C utilisent le bus master ESP-IDF v5.x.
 * La calibration et les LSB sont calcules selon les formules du datasheet.
 */

#include "bmu_ina237.h"
#include "bmu_i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <cmath>
#include <cstring>

static const char *TAG = "INA237";

/* Timeout I2C en ticks FreeRTOS (50 ms, adapte au bus 50 kHz) */
#define I2C_TIMEOUT_MS  50

/* ── Fonctions I2C bas niveau ─────────────────────────────────────────────── */

/**
 * @brief Ecrit un registre 16 bits (MSB first).
 */
static esp_err_t ina237_write_reg16(i2c_master_dev_handle_t dev,
                                    uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFF)
    };
    return i2c_master_transmit(dev, buf, sizeof(buf), pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

/**
 * @brief Lit un registre 16 bits (MSB first).
 */
static esp_err_t ina237_read_reg16(i2c_master_dev_handle_t dev,
                                   uint8_t reg, uint16_t *value)
{
    uint8_t tx = reg;
    uint8_t rx[2] = {0};

    esp_err_t ret = i2c_master_transmit_receive(dev, &tx, 1, rx, 2,
                                                pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret == ESP_OK) {
        *value = ((uint16_t)rx[0] << 8) | rx[1];
    }
    return ret;
}

/**
 * @brief Lit un registre 24 bits (Power register, 3 octets MSB first).
 */
static esp_err_t ina237_read_reg24(i2c_master_dev_handle_t dev,
                                   uint8_t reg, uint32_t *value)
{
    uint8_t tx = reg;
    uint8_t rx[3] = {0};

    esp_err_t ret = i2c_master_transmit_receive(dev, &tx, 1, rx, 3,
                                                pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    if (ret == ESP_OK) {
        *value = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];
    }
    return ret;
}

/* ── Initialisation ───────────────────────────────────────────────────────── */

esp_err_t bmu_ina237_init(i2c_master_bus_handle_t bus, uint8_t addr,
                          uint32_t r_shunt_uohm, float max_current_a,
                          bmu_ina237_t *ctx)
{
    if (ctx == NULL) return ESP_ERR_INVALID_ARG;
    memset(ctx, 0, sizeof(*ctx));
    ctx->addr = addr;
    ctx->ready = false;

    /* Ajouter le device sur le bus I2C */
    esp_err_t ret = bmu_i2c_add_device(bus, addr, &ctx->dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[0x%02X] Echec ajout device I2C: %s", addr, esp_err_to_name(ret));
        return ret;
    }

    /* 1. Verification identite — MANUFACTURER_ID doit etre 0x5449 ("TI") */
    uint16_t mfr_id = 0;
    ret = ina237_read_reg16(ctx->dev, INA237_REG_MANUFACTURER_ID, &mfr_id);
    if (ret != ESP_OK || mfr_id != INA237_MANUFACTURER_ID) {
        ESP_LOGE(TAG, "[0x%02X] ID fabricant invalide: 0x%04X (attendu 0x%04X)",
                 addr, mfr_id, INA237_MANUFACTURER_ID);
        i2c_master_bus_rm_device(ctx->dev);
        ctx->dev = NULL;
        return (ret != ESP_OK) ? ret : ESP_ERR_NOT_FOUND;
    }

    /* Verification DEVICE_ID — famille 0x23xx (INA237 = 0x2370, INA237A = 0x2381) */
    uint16_t dev_id = 0;
    ret = ina237_read_reg16(ctx->dev, INA237_REG_DEVICE_ID, &dev_id);
    if (ret != ESP_OK || (dev_id & INA237_DEVICE_ID_MASK) != INA237_DEVICE_ID_FAMILY) {
        ESP_LOGE(TAG, "[0x%02X] Device ID invalide: 0x%04X (attendu 0x23xx)",
                 addr, dev_id);
        i2c_master_bus_rm_device(ctx->dev);
        ctx->dev = NULL;
        return (ret != ESP_OK) ? ret : ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "[0x%02X] INA237 detecte (MFR=0x%04X DEV=0x%04X)", addr, mfr_id, dev_id);

    /* 2. Reset device (CONFIG bit 15) */
    ret = ina237_write_reg16(ctx->dev, INA237_REG_CONFIG, INA237_CONFIG_RST);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[0x%02X] Echec reset: %s", addr, esp_err_to_name(ret));
        return ret;
    }
    /* Attente post-reset (datasheet : typique 1 ms) */
    vTaskDelay(pdMS_TO_TICKS(2));

    /* 3. CONFIG : ADCRANGE=0 (+-163.84 mV), pas de delai de conversion */
    ret = ina237_write_reg16(ctx->dev, INA237_REG_CONFIG, INA237_CONFIG_ADCRANGE_0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[0x%02X] Echec ecriture CONFIG: %s", addr, esp_err_to_name(ret));
        return ret;
    }

    /* 4. Calcul et ecriture SHUNT_CAL (datasheet SBOS945 section 8.1.2)
     *    CURRENT_LSB = Max_Expected_Current / 2^15
     *    SHUNT_CAL   = 819.2e6 * CURRENT_LSB * R_SHUNT
     *    R_SHUNT en ohms = r_shunt_uohm / 1e6                               */
    ctx->current_lsb = max_current_a / 32768.0f;
    float r_shunt_ohm = (float)r_shunt_uohm / 1000000.0f;
    float shunt_cal_f = 819.2e6f * ctx->current_lsb * r_shunt_ohm;
    uint16_t shunt_cal = (uint16_t)(shunt_cal_f + 0.5f);

    if (shunt_cal == 0) {
        ESP_LOGE(TAG, "[0x%02X] SHUNT_CAL calcule a 0 — parametres invalides", addr);
        return ESP_ERR_INVALID_ARG;
    }

    ret = ina237_write_reg16(ctx->dev, INA237_REG_SHUNT_CAL, shunt_cal);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[0x%02X] Echec ecriture SHUNT_CAL: %s", addr, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "[0x%02X] CURRENT_LSB=%.6f A/bit, R_SHUNT=%.4f Ohm, SHUNT_CAL=%u",
             addr, ctx->current_lsb, r_shunt_ohm, shunt_cal);

    /* 5. ADC_CONFIG optimise BMU : continu bus+shunt, 540us, 64 moyennes */
    ret = ina237_write_reg16(ctx->dev, INA237_REG_ADC_CONFIG, INA237_ADC_CONFIG_BMU);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[0x%02X] Echec ecriture ADC_CONFIG: %s", addr, esp_err_to_name(ret));
        return ret;
    }

    /* 6. Alertes bus tension : BOVL et BUVL
     *    Bus voltage LSB = 3.125 mV, registre = voltage_mV / 3.125          */
    ret = bmu_ina237_set_bus_voltage_alerts(ctx, 30000, 24000);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "[0x%02X] Echec config alertes tension (non fatal)", addr);
        /* Non fatal — on continue */
    }

    ctx->ready = true;
    ESP_LOGI(TAG, "[0x%02X] INA237 initialise OK", addr);
    return ESP_OK;
}

/* ── Lectures ─────────────────────────────────────────────────────────────── */

esp_err_t bmu_ina237_read_bus_voltage(const bmu_ina237_t *ctx, float *voltage_mv)
{
    if (ctx == NULL || !ctx->ready || voltage_mv == NULL) return ESP_ERR_INVALID_ARG;

    uint16_t raw = 0;
    esp_err_t ret = ina237_read_reg16(ctx->dev, INA237_REG_VBUS, &raw);
    if (ret != ESP_OK) {
        bmu_i2c_record_failure();
        return ret;
    }
    bmu_i2c_record_success();

    /* Bus voltage : unsigned, LSB = 3.125 mV */
    *voltage_mv = (float)raw * 3.125f;
    return ESP_OK;
}

esp_err_t bmu_ina237_read_current(const bmu_ina237_t *ctx, float *current_a)
{
    if (ctx == NULL || !ctx->ready || current_a == NULL) return ESP_ERR_INVALID_ARG;

    uint16_t raw = 0;
    esp_err_t ret = ina237_read_reg16(ctx->dev, INA237_REG_CURRENT, &raw);
    if (ret != ESP_OK) {
        bmu_i2c_record_failure();
        return ret;
    }
    bmu_i2c_record_success();

    /* Current : signed 16-bit, LSB = CURRENT_LSB A/bit */
    *current_a = (float)(int16_t)raw * ctx->current_lsb;
    return ESP_OK;
}

esp_err_t bmu_ina237_read_power(const bmu_ina237_t *ctx, float *power_w)
{
    if (ctx == NULL || !ctx->ready || power_w == NULL) return ESP_ERR_INVALID_ARG;

    uint32_t raw = 0;
    esp_err_t ret = ina237_read_reg24(ctx->dev, INA237_REG_POWER, &raw);
    if (ret != ESP_OK) return ret;

    /* Power : unsigned 24-bit, LSB = 0.2 * CURRENT_LSB W/bit */
    *power_w = (float)raw * 0.2f * ctx->current_lsb;
    return ESP_OK;
}

esp_err_t bmu_ina237_read_temperature(const bmu_ina237_t *ctx, float *temp_c)
{
    if (ctx == NULL || !ctx->ready || temp_c == NULL) return ESP_ERR_INVALID_ARG;

    uint16_t raw = 0;
    esp_err_t ret = ina237_read_reg16(ctx->dev, INA237_REG_DIETEMP, &raw);
    if (ret != ESP_OK) return ret;

    /* Temperature : signed 16-bit, bits 15-4 valides, right-shift 4.
     * LSB = 125 m degC/bit apres shift.                                     */
    int16_t raw_signed = (int16_t)raw;
    *temp_c = (float)(raw_signed >> 4) * 0.125f;
    return ESP_OK;
}

esp_err_t bmu_ina237_read_voltage_current(const bmu_ina237_t *ctx,
                                          float *voltage_mv, float *current_a)
{
    if (ctx == NULL || !ctx->ready || voltage_mv == NULL || current_a == NULL)
        return ESP_ERR_INVALID_ARG;

    /* Acquire I2C bus mutex for atomic V+I read (audit H-01) */
    if (bmu_i2c_lock() != ESP_OK) {
        *voltage_mv = NAN;
        *current_a = NAN;
        return ESP_ERR_TIMEOUT;
    }

    /* Lecture sequentielle bus voltage puis current */
    esp_err_t ret = bmu_ina237_read_bus_voltage(ctx, voltage_mv);
    if (ret != ESP_OK) {
        bmu_i2c_unlock();
        return ret;
    }

    ret = bmu_ina237_read_current(ctx, current_a);
    bmu_i2c_unlock();
    return ret;
}

/* ── Configuration alertes ────────────────────────────────────────────────── */

esp_err_t bmu_ina237_set_bus_voltage_alerts(const bmu_ina237_t *ctx,
                                            uint32_t overvoltage_mv,
                                            uint32_t undervoltage_mv)
{
    if (ctx == NULL || ctx->dev == NULL) return ESP_ERR_INVALID_ARG;

    /* BOVL/BUVL : tension en mV / 3.125 mV/LSB, unsigned 16-bit */
    uint16_t bovl = (uint16_t)(overvoltage_mv * 1000U / 3125U);
    uint16_t buvl = (uint16_t)(undervoltage_mv * 1000U / 3125U);

    esp_err_t ret = ina237_write_reg16(ctx->dev, INA237_REG_BOVL, bovl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[0x%02X] Echec ecriture BOVL: %s", ctx->addr, esp_err_to_name(ret));
        return ret;
    }

    ret = ina237_write_reg16(ctx->dev, INA237_REG_BUVL, buvl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "[0x%02X] Echec ecriture BUVL: %s", ctx->addr, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "[0x%02X] Alertes bus: BOVL=%" PRIu32 " mV (reg=%u) BUVL=%" PRIu32 " mV (reg=%u)",
             ctx->addr, overvoltage_mv, bovl, undervoltage_mv, buvl);
    return ESP_OK;
}

esp_err_t bmu_ina237_read_diag_alert(const bmu_ina237_t *ctx, uint16_t *flags)
{
    if (ctx == NULL || !ctx->ready || flags == NULL) return ESP_ERR_INVALID_ARG;

    return ina237_read_reg16(ctx->dev, INA237_REG_DIAG_ALRT, flags);
}

/* ── Scan et initialisation en masse ──────────────────────────────────────── */

esp_err_t bmu_ina237_scan_init(i2c_master_bus_handle_t bus,
                               uint32_t r_shunt_uohm, float max_current_a,
                               bmu_ina237_t devices[], uint8_t *count)
{
    if (devices == NULL || count == NULL) return ESP_ERR_INVALID_ARG;

    *count = 0;
    ESP_LOGI(TAG, "Scan INA237 sur bus I2C (0x%02X-0x%02X)...", INA237_ADDR_MIN, INA237_ADDR_MAX);

    for (uint8_t addr = INA237_ADDR_MIN; addr <= INA237_ADDR_MAX; addr++) {
        if (*count >= INA237_MAX_DEVICES) break;

        /* Tenter un probe I2C avant init complete */
        if (i2c_master_probe(bus, addr, pdMS_TO_TICKS(50)) != ESP_OK) {
            continue;
        }

        esp_err_t ret = bmu_ina237_init(bus, addr, r_shunt_uohm, max_current_a,
                                        &devices[*count]);
        if (ret == ESP_OK) {
            (*count)++;
        } else {
            ESP_LOGW(TAG, "[0x%02X] Probe OK mais init echouee: %s",
                     addr, esp_err_to_name(ret));
        }
    }

    ESP_LOGI(TAG, "Scan termine: %u INA237 initialise(s)", (unsigned)*count);
    return (*count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/* ── Variantes bit-bang ───────────────────────────────────────────────────── */

#if CONFIG_BMU_I2C_BB_ENABLED

esp_err_t bmu_ina237_bb_init(bmu_i2c_bb_handle_t bb, uint8_t addr,
                              uint32_t r_shunt_uohm, float max_current_a,
                              bmu_ina237_bb_t *ctx)
{
    ctx->bb = bb;
    ctx->addr = addr;
    ctx->ready = false;

    uint16_t mfr = 0;
    esp_err_t ret = bmu_i2c_bb_read_reg16(bb, addr, INA237_REG_MANUFACTURER_ID, &mfr);
    if (ret != ESP_OK || mfr != INA237_MANUFACTURER_ID) {
        ESP_LOGW(TAG, "[0x%02X] BB not INA237 (MFR=0x%04X)", addr, mfr);
        return ESP_ERR_NOT_FOUND;
    }

    bmu_i2c_bb_write_reg16(bb, addr, INA237_REG_CONFIG, INA237_CONFIG_RST);
    vTaskDelay(pdMS_TO_TICKS(2));

    ctx->current_lsb = max_current_a / 32768.0f;
    float shunt_cal = 819.2e6f * ctx->current_lsb * (float)r_shunt_uohm / 1e6f;
    uint16_t cal_reg = (uint16_t)(shunt_cal + 0.5f);
    bmu_i2c_bb_write_reg16(bb, addr, INA237_REG_SHUNT_CAL, cal_reg);
    bmu_i2c_bb_write_reg16(bb, addr, INA237_REG_ADC_CONFIG, INA237_ADC_CONFIG_BMU);

    ctx->ready = true;
    ESP_LOGI(TAG, "[0x%02X] BB INA237 init OK", addr);
    return ESP_OK;
}

esp_err_t bmu_ina237_bb_read_bus_voltage(const bmu_ina237_bb_t *ctx, float *voltage_mv)
{
    if (!ctx->ready) return ESP_ERR_INVALID_STATE;
    uint16_t raw;
    esp_err_t ret = bmu_i2c_bb_read_reg16(ctx->bb, ctx->addr, INA237_REG_VBUS, &raw);
    if (ret == ESP_OK) *voltage_mv = (float)(int16_t)raw * (float)INA237_VBUS_LSB_UV / 1000.0f;
    return ret;
}

esp_err_t bmu_ina237_bb_read_current(const bmu_ina237_bb_t *ctx, float *current_a)
{
    if (!ctx->ready) return ESP_ERR_INVALID_STATE;
    uint16_t raw;
    esp_err_t ret = bmu_i2c_bb_read_reg16(ctx->bb, ctx->addr, INA237_REG_CURRENT, &raw);
    if (ret == ESP_OK) *current_a = (float)(int16_t)raw * ctx->current_lsb;
    return ret;
}

esp_err_t bmu_ina237_bb_read_voltage_current(const bmu_ina237_bb_t *ctx,
                                              float *voltage_mv, float *current_a)
{
    esp_err_t ret = bmu_ina237_bb_read_bus_voltage(ctx, voltage_mv);
    if (ret != ESP_OK) return ret;
    return bmu_ina237_bb_read_current(ctx, current_a);
}

esp_err_t bmu_ina237_bb_scan_init(bmu_i2c_bb_handle_t bb,
                                   uint32_t r_shunt_uohm, float max_current_a,
                                   bmu_ina237_bb_t devices[], uint8_t *count)
{
    *count = 0;
    ESP_LOGI(TAG, "BB scan INA237 (0x%02X-0x%02X)...", INA237_ADDR_MIN, INA237_ADDR_MAX);
    for (uint8_t addr = INA237_ADDR_MIN; addr <= INA237_ADDR_MAX; addr++) {
        if (!bmu_i2c_bb_probe(bb, addr)) continue;
        if (*count >= INA237_MAX_DEVICES) break;
        if (bmu_ina237_bb_init(bb, addr, r_shunt_uohm, max_current_a, &devices[*count]) == ESP_OK)
            (*count)++;
    }
    ESP_LOGI(TAG, "BB scan: %d INA237", *count);
    return (*count > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

#endif /* CONFIG_BMU_I2C_BB_ENABLED */
