/**
 * @file bmu_tca9535.cpp
 * @brief Driver TCA9535 pour BMU v2 — basé sur datasheet TI SCPS209
 *
 * Gère les GPIO expanders TCA9535 sur le bus I2C du BMU :
 *   - 4 switches MOSFET batterie (Port 0, bits 3-0)
 *   - 4 entrées alerte INA237   (Port 0, bits 7-4)
 *   - 8 LEDs (4×rouge + 4×vert) (Port 1, bits 7-0)
 *
 * Les registres Output sont cachés localement pour éviter les
 * read-modify-write sur le bus I2C.
 */

#include "bmu_tca9535.h"
#include "bmu_i2c.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "bmu_tca9535";

/* ========================================================================
 * Helpers I2C bas niveau
 * ======================================================================== */

/**
 * Ecriture d'un octet dans un registre du TCA9535.
 * Protocole : START | ADDR+W | reg | data | STOP
 */
static esp_err_t tca9535_write_reg8(i2c_master_dev_handle_t dev,
                                    uint8_t reg, uint8_t data)
{
    if (bmu_i2c_lock() != ESP_OK) return ESP_ERR_TIMEOUT;
    uint8_t buf[2] = { reg, data };
    esp_err_t ret = i2c_master_transmit(dev, buf, sizeof(buf), pdMS_TO_TICKS(50));
    bmu_i2c_unlock();
    return ret;
}

/**
 * Ecriture de deux octets consecutifs (Port0 + Port1) via auto-increment.
 * Protocole : START | ADDR+W | reg | data_p0 | data_p1 | STOP
 */
static esp_err_t tca9535_write_reg16(i2c_master_dev_handle_t dev,
                                     uint8_t reg,
                                     uint8_t data_p0, uint8_t data_p1)
{
    if (bmu_i2c_lock() != ESP_OK) return ESP_ERR_TIMEOUT;
    uint8_t buf[3] = { reg, data_p0, data_p1 };
    esp_err_t ret = i2c_master_transmit(dev, buf, sizeof(buf), pdMS_TO_TICKS(50));
    bmu_i2c_unlock();
    return ret;
}

/**
 * Lecture d'un octet depuis un registre du TCA9535.
 * Protocole : START | ADDR+W | reg | RESTART | ADDR+R | data | STOP
 */
static esp_err_t tca9535_read_reg8(i2c_master_dev_handle_t dev,
                                   uint8_t reg, uint8_t *data)
{
    if (bmu_i2c_lock() != ESP_OK) return ESP_ERR_TIMEOUT;
    esp_err_t ret = i2c_master_transmit_receive(dev, &reg, 1, data, 1, pdMS_TO_TICKS(50));
    bmu_i2c_unlock();
    return ret;
}

/* ========================================================================
 * Initialisation
 * ======================================================================== */

/**
 * Configure un TCA9535 : directions + sorties a LOW.
 * Appelé par init et scan_init.
 */
static esp_err_t tca9535_configure(bmu_tca9535_handle_t *handle)
{
    esp_err_t ret;

    /* --- Forcer toutes les sorties a LOW avant de configurer la direction ---
     * Cela evite toute impulsion transitoire lors du passage en mode output.
     * On ecrit Output Port 0 et Output Port 1 en une seule transaction.
     */
    handle->out_p0 = 0x00;
    handle->out_p1 = 0x00;
    ret = tca9535_write_reg16(handle->dev,
                              TCA9535_REG_OUTPUT_PORT0,
                              handle->out_p0, handle->out_p1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur ecriture Output Ports @ 0x%02X : %s",
                 handle->addr, esp_err_to_name(ret));
        return ret;
    }

    /* --- Configurer les directions ---
     * Port 0 : 0xF0 = bits 7-4 input (alertes), bits 3-0 output (switches)
     * Port 1 : 0x00 = tout en output (LEDs)
     */
    ret = tca9535_write_reg16(handle->dev,
                              TCA9535_REG_CONFIG_PORT0,
                              BMU_TCA_CONFIG_PORT0, BMU_TCA_CONFIG_PORT1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur ecriture Config Ports @ 0x%02X : %s",
                 handle->addr, esp_err_to_name(ret));
        return ret;
    }

    /* Pas d'inversion de polarite */
    ret = tca9535_write_reg16(handle->dev,
                              TCA9535_REG_POLARITY_INV0,
                              0x00, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur ecriture Polarity @ 0x%02X : %s",
                 handle->addr, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "TCA9535 @ 0x%02X configure (P0=0x%02X, P1=0x%02X)",
             handle->addr, BMU_TCA_CONFIG_PORT0, BMU_TCA_CONFIG_PORT1);
    return ESP_OK;
}

esp_err_t bmu_tca9535_init(i2c_master_bus_handle_t bus,
                           uint8_t                 addr,
                           bmu_tca9535_handle_t   *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (addr < TCA9535_BASE_ADDR || addr > (TCA9535_BASE_ADDR + TCA9535_MAX_DEVICES - 1)) {
        ESP_LOGE(TAG, "Adresse I2C hors plage TCA9535 : 0x%02X", addr);
        return ESP_ERR_INVALID_ARG;
    }

    memset(handle, 0, sizeof(*handle));
    handle->addr = addr;

    esp_err_t ret = bmu_i2c_add_device(bus, addr, &handle->dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Impossible d'ajouter device I2C @ 0x%02X : %s",
                 addr, esp_err_to_name(ret));
        return ret;
    }

    return tca9535_configure(handle);
}

esp_err_t bmu_tca9535_scan_init(i2c_master_bus_handle_t bus,
                                bmu_tca9535_handle_t   *handles,
                                uint8_t                 max_devices,
                                uint8_t                *found)
{
    if (handles == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *found = 0;

    for (uint8_t i = 0; i < TCA9535_MAX_DEVICES && *found < max_devices; i++) {
        uint8_t addr = TCA9535_BASE_ADDR + i;

        /* Tenter de lire le registre Input Port 0 pour detecter la presence */
        i2c_master_dev_handle_t dev;
        esp_err_t ret = bmu_i2c_add_device(bus, addr, &dev);
        if (ret != ESP_OK) {
            continue;
        }

        uint8_t dummy;
        ret = tca9535_read_reg8(dev, TCA9535_REG_INPUT_PORT0, &dummy);
        if (ret != ESP_OK) {
            /* Device non present a cette adresse — liberer le handle et continuer */
            i2c_master_bus_rm_device(dev);
            ESP_LOGD(TAG, "Pas de TCA9535 @ 0x%02X", addr);
            continue;
        }

        /* Device detecte — configurer */
        bmu_tca9535_handle_t *h = &handles[*found];
        memset(h, 0, sizeof(*h));
        h->dev  = dev;
        h->addr = addr;

        ret = tca9535_configure(h);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "TCA9535 @ 0x%02X detecte mais erreur de configuration", addr);
            continue;
        }

        (*found)++;
        ESP_LOGI(TAG, "TCA9535 #%u detecte @ 0x%02X", *found, addr);
    }

    ESP_LOGI(TAG, "Scan termine : %u TCA9535 trouves", *found);
    return (*found > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

/* ========================================================================
 * Commandes batterie
 * ======================================================================== */

/**
 * Mapping PCB : canal batterie → bit dans Port 0 (numeros inverses)
 *   channel 0 (bat1) → bit 3 (P0.3)
 *   channel 1 (bat2) → bit 2 (P0.2)
 *   channel 2 (bat3) → bit 1 (P0.1)
 *   channel 3 (bat4) → bit 0 (P0.0)
 */
static inline uint8_t switch_bit(uint8_t channel)
{
    return (uint8_t)(3 - channel);
}

/**
 * Mapping PCB : canal batterie → bit d'alerte dans Port 0 (numeros inverses)
 *   channel 0 (bat1) → bit 7 (P0.7)
 *   channel 1 (bat2) → bit 6 (P0.6)
 *   channel 2 (bat3) → bit 5 (P0.5)
 *   channel 3 (bat4) → bit 4 (P0.4)
 */
static inline uint8_t alert_bit(uint8_t channel)
{
    return (uint8_t)(7 - channel);
}

esp_err_t bmu_tca9535_switch_battery(bmu_tca9535_handle_t *handle,
                                     uint8_t               channel,
                                     bool                  on)
{
    if (handle == NULL || channel >= BMU_TCA_CHANNELS_PER_DEVICE) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t bit = switch_bit(channel);

    if (on) {
        handle->out_p0 |= (1 << bit);
    } else {
        handle->out_p0 &= ~(1 << bit);
    }

    esp_err_t ret = tca9535_write_reg8(handle->dev,
                                       TCA9535_REG_OUTPUT_PORT0,
                                       handle->out_p0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur switch batterie ch%u @ 0x%02X : %s",
                 channel, handle->addr, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t bmu_tca9535_set_led(bmu_tca9535_handle_t *handle,
                              uint8_t               channel,
                              bool                  red,
                              bool                  green)
{
    if (handle == NULL || channel >= BMU_TCA_CHANNELS_PER_DEVICE) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Mapping LED : channel N → red = bit (2*N), green = bit (2*N + 1) */
    uint8_t red_bit   = (uint8_t)(channel * 2);
    uint8_t green_bit = (uint8_t)(channel * 2 + 1);

    if (red) {
        handle->out_p1 |= (1 << red_bit);
    } else {
        handle->out_p1 &= ~(1 << red_bit);
    }

    if (green) {
        handle->out_p1 |= (1 << green_bit);
    } else {
        handle->out_p1 &= ~(1 << green_bit);
    }

    esp_err_t ret = tca9535_write_reg8(handle->dev,
                                       TCA9535_REG_OUTPUT_PORT1,
                                       handle->out_p1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur LED ch%u @ 0x%02X : %s",
                 channel, handle->addr, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t bmu_tca9535_read_alert(bmu_tca9535_handle_t *handle,
                                 uint8_t               channel,
                                 bool                  *alert)
{
    if (handle == NULL || alert == NULL || channel >= BMU_TCA_CHANNELS_PER_DEVICE) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t input_p0;
    esp_err_t ret = tca9535_read_reg8(handle->dev,
                                      TCA9535_REG_INPUT_PORT0,
                                      &input_p0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur lecture alerte ch%u @ 0x%02X : %s",
                 channel, handle->addr, esp_err_to_name(ret));
        return ret;
    }

    uint8_t bit = alert_bit(channel);
    /* Alerte active = pin LOW (active-low) */
    *alert = !(input_p0 & (1 << bit));

    return ESP_OK;
}

esp_err_t bmu_tca9535_all_off(bmu_tca9535_handle_t *handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    handle->out_p0 = 0x00;
    handle->out_p1 = 0x00;

    /* Ecriture bulk des deux ports en une transaction */
    esp_err_t ret = tca9535_write_reg16(handle->dev,
                                        TCA9535_REG_OUTPUT_PORT0,
                                        handle->out_p0, handle->out_p1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erreur all_off @ 0x%02X : %s",
                 handle->addr, esp_err_to_name(ret));
    }
    return ret;
}

/* ── Variantes bit-bang ───────────────────────────────────────────────────── */

#if CONFIG_BMU_I2C_BB_ENABLED

static esp_err_t tca_bb_init_one(bmu_i2c_bb_handle_t bb, uint8_t addr,
                                  bmu_tca9535_bb_handle_t *h)
{
    h->bb = bb;
    h->addr = addr;
    h->out_p0 = 0x00;
    h->out_p1 = 0x00;
    esp_err_t ret;
    ret = bmu_i2c_bb_write_reg16(bb, addr, TCA9535_REG_CONFIG_PORT0,
                                  ((uint16_t)BMU_TCA_CONFIG_PORT1 << 8) | BMU_TCA_CONFIG_PORT0);
    if (ret != ESP_OK) return ret;
    ret = bmu_i2c_bb_write_reg16(bb, addr, TCA9535_REG_OUTPUT_PORT0, 0x0000);
    ESP_LOGI("TCA_BB", "[0x%02X] BB TCA9535 init OK", addr);
    return ret;
}

esp_err_t bmu_tca9535_bb_scan_init(bmu_i2c_bb_handle_t bb,
                                    bmu_tca9535_bb_handle_t *handles,
                                    uint8_t max_devices, uint8_t *found)
{
    *found = 0;
    for (uint8_t addr = TCA9535_BASE_ADDR; addr < TCA9535_BASE_ADDR + TCA9535_MAX_DEVICES; addr++) {
        if (*found >= max_devices) break;
        if (!bmu_i2c_bb_probe(bb, addr)) continue;
        if (tca_bb_init_one(bb, addr, &handles[*found]) == ESP_OK)
            (*found)++;
    }
    ESP_LOGI("TCA_BB", "BB scan: %d TCA9535", *found);
    return (*found > 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t bmu_tca9535_bb_switch_battery(bmu_tca9535_bb_handle_t *h,
                                         uint8_t channel, bool on)
{
    if (channel > 3) return ESP_ERR_INVALID_ARG;
    uint8_t bit = 3 - channel; /* mapping inverse PCB */
    if (on) h->out_p0 |= (1 << bit); else h->out_p0 &= ~(1 << bit);
    uint8_t buf[2] = { TCA9535_REG_OUTPUT_PORT0, h->out_p0 };
    return bmu_i2c_bb_write(h->bb, h->addr, buf, 2);
}

esp_err_t bmu_tca9535_bb_set_led(bmu_tca9535_bb_handle_t *h,
                                  uint8_t channel, bool red, bool green)
{
    if (channel > 3) return ESP_ERR_INVALID_ARG;
    uint8_t r_bit = channel * 2;
    uint8_t g_bit = channel * 2 + 1;
    if (red)   h->out_p1 |= (1 << r_bit);  else h->out_p1 &= ~(1 << r_bit);
    if (green) h->out_p1 |= (1 << g_bit);  else h->out_p1 &= ~(1 << g_bit);
    uint8_t buf[2] = { TCA9535_REG_OUTPUT_PORT1, h->out_p1 };
    return bmu_i2c_bb_write(h->bb, h->addr, buf, 2);
}

esp_err_t bmu_tca9535_bb_all_off(bmu_tca9535_bb_handle_t *h)
{
    h->out_p0 = 0;
    h->out_p1 = 0;
    bmu_i2c_bb_write_reg16(h->bb, h->addr, TCA9535_REG_OUTPUT_PORT0, 0x0000);
    return ESP_OK;
}

#endif /* CONFIG_BMU_I2C_BB_ENABLED */
