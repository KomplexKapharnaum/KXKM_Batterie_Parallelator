#pragma once

/**
 * @file bmu_ina237.h
 * @brief Driver INA237 pour BMU — operations registre conformes datasheet TI SBOS945.
 *
 * Shunt : 2 mOhm, ADCRANGE=0 (+-163.84 mV), courant max mesurable ~81.92 A.
 * Adresses I2C : 0x40-0x4F (16 capteurs max via solder jumpers JP17-JP20).
 */

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Registres INA237 (SBOS945 Table 7-5) ────────────────────────────────── */
#define INA237_REG_CONFIG          0x00
#define INA237_REG_ADC_CONFIG      0x01
#define INA237_REG_SHUNT_CAL       0x02
#define INA237_REG_VSHUNT          0x04
#define INA237_REG_VBUS            0x05
#define INA237_REG_DIETEMP         0x06
#define INA237_REG_CURRENT         0x07
#define INA237_REG_POWER           0x08
#define INA237_REG_DIAG_ALRT       0x0B
#define INA237_REG_SOVL            0x0C
#define INA237_REG_SUVL            0x0D
#define INA237_REG_BOVL            0x0E
#define INA237_REG_BUVL            0x0F
#define INA237_REG_TEMP_LIMIT      0x10
#define INA237_REG_PWR_LIMIT       0x11
#define INA237_REG_MANUFACTURER_ID 0x3E
#define INA237_REG_DEVICE_ID       0x3F

/* ── Valeurs d'identification ─────────────────────────────────────────────── */
#define INA237_MANUFACTURER_ID     0x5449   /* "TI" ASCII */
#define INA237_DEVICE_ID           0x2370
#define INA237_DEVICE_ID_FAMILY    0x2300  /* Famille INA237/INA237A (0x23xx) */
#define INA237_DEVICE_ID_MASK      0xFF00

/* ── CONFIG bits ──────────────────────────────────────────────────────────── */
#define INA237_CONFIG_RST          (1U << 15)
#define INA237_CONFIG_RSTACC       (1U << 14)
#define INA237_CONFIG_ADCRANGE_0   0x0000   /* +-163.84 mV */
#define INA237_CONFIG_ADCRANGE_1   (1U << 4) /* +-40.96 mV  */

/* ── ADC_CONFIG optimise BMU ──────────────────────────────────────────────── *
 * MODE=1011 (continu bus+shunt), VBUSCT=100 (540us), VSHCT=100 (540us),
 * VTCT=000 (50us, inutilise), AVG=011 (64 moyennes)
 * = 0xB903                                                                   */
#define INA237_ADC_CONFIG_BMU      0xB903

/* ── LSB values (ADCRANGE=0) ──────────────────────────────────────────────── */
#define INA237_VBUS_LSB_UV         3125     /* 3.125 mV = 3125 uV par bit    */
#define INA237_VSHUNT_LSB_NV       5000     /* 5 uV = 5000 nV par bit        */
#define INA237_TEMP_LSB_MDEGC      125      /* 125 m degC par bit             */

/* ── Limites de scan ──────────────────────────────────────────────────────── */
#define INA237_ADDR_MIN            0x40
#define INA237_ADDR_MAX            0x4F
#define INA237_MAX_DEVICES         16

/* ── Hardware BMU v2 ──────────────────────────────────────────────────────── */
#define INA237_SHUNT_RESISTANCE_UOHM  2000  /* 2 mOhm = 2000 uOhm           */

/**
 * @brief Contexte d'un capteur INA237 initialise.
 */
typedef struct {
    i2c_master_dev_handle_t dev;     /**< Handle I2C device ESP-IDF          */
    uint8_t                 addr;    /**< Adresse 7-bit (0x40-0x4F)         */
    float                   current_lsb; /**< CURRENT_LSB en A/bit          */
    bool                    ready;   /**< true si init+calibration OK       */
} bmu_ina237_t;

/* ── API publique ─────────────────────────────────────────────────────────── */

/**
 * @brief Initialise un INA237 : reset, verification ID, calibration, config ADC.
 *
 * @param bus       Handle du bus I2C maitre (bmu_i2c)
 * @param addr      Adresse 7-bit du capteur (0x40-0x4F)
 * @param r_shunt_uohm Resistance shunt en micro-ohms (2000 pour 2 mOhm)
 * @param max_current_a Courant max attendu en A (pour calcul CURRENT_LSB)
 * @param[out] ctx  Contexte capteur rempli si ESP_OK
 * @return ESP_OK ou code erreur
 */
esp_err_t bmu_ina237_init(i2c_master_bus_handle_t bus, uint8_t addr,
                          uint32_t r_shunt_uohm, float max_current_a,
                          bmu_ina237_t *ctx);

/**
 * @brief Lit la tension bus en mV.
 */
esp_err_t bmu_ina237_read_bus_voltage(const bmu_ina237_t *ctx, float *voltage_mv);

/**
 * @brief Lit le courant en A (signe, negatif = decharge).
 */
esp_err_t bmu_ina237_read_current(const bmu_ina237_t *ctx, float *current_a);

/**
 * @brief Lit la puissance en W (non signe, 24 bits).
 */
esp_err_t bmu_ina237_read_power(const bmu_ina237_t *ctx, float *power_w);

/**
 * @brief Lit la temperature die en degres C.
 */
esp_err_t bmu_ina237_read_temperature(const bmu_ina237_t *ctx, float *temp_c);

/**
 * @brief Lit tension bus (mV) et courant (A) en une seule sequence.
 */
esp_err_t bmu_ina237_read_voltage_current(const bmu_ina237_t *ctx,
                                          float *voltage_mv, float *current_a);

/**
 * @brief Configure les seuils d'alerte bus over/under voltage.
 * @param overvoltage_mv Seuil sur-tension en mV (ex: 30000)
 * @param undervoltage_mv Seuil sous-tension en mV (ex: 24000)
 */
esp_err_t bmu_ina237_set_bus_voltage_alerts(const bmu_ina237_t *ctx,
                                            uint32_t overvoltage_mv,
                                            uint32_t undervoltage_mv);

/**
 * @brief Lit le registre DIAG_ALRT (flags diagnostic + alertes).
 */
esp_err_t bmu_ina237_read_diag_alert(const bmu_ina237_t *ctx, uint16_t *flags);

/**
 * @brief Scanne le bus I2C 0x40-0x4F, initialise tous les INA237 detectes.
 *
 * @param bus           Handle du bus I2C maitre
 * @param r_shunt_uohm Resistance shunt en micro-ohms
 * @param max_current_a Courant max attendu en A
 * @param[out] devices  Tableau de INA237_MAX_DEVICES contextes
 * @param[out] count    Nombre de capteurs initialises
 * @return ESP_OK si au moins 1 capteur trouve, ESP_ERR_NOT_FOUND sinon
 */
esp_err_t bmu_ina237_scan_init(i2c_master_bus_handle_t bus,
                               uint32_t r_shunt_uohm, float max_current_a,
                               bmu_ina237_t devices[], uint8_t *count);

/* ── Bit-bang bus variants ────────────────────────────────────────── */
#ifdef CONFIG_BMU_I2C_BB_ENABLED
#include "bmu_i2c_bitbang.h"

typedef struct {
    bmu_i2c_bb_handle_t bb;
    uint8_t             addr;
    float               current_lsb;
    bool                ready;
} bmu_ina237_bb_t;

esp_err_t bmu_ina237_bb_init(bmu_i2c_bb_handle_t bb, uint8_t addr,
                              uint32_t r_shunt_uohm, float max_current_a,
                              bmu_ina237_bb_t *ctx);
esp_err_t bmu_ina237_bb_read_bus_voltage(const bmu_ina237_bb_t *ctx, float *voltage_mv);
esp_err_t bmu_ina237_bb_read_current(const bmu_ina237_bb_t *ctx, float *current_a);
esp_err_t bmu_ina237_bb_read_voltage_current(const bmu_ina237_bb_t *ctx,
                                              float *voltage_mv, float *current_a);
esp_err_t bmu_ina237_bb_scan_init(bmu_i2c_bb_handle_t bb,
                                   uint32_t r_shunt_uohm, float max_current_a,
                                   bmu_ina237_bb_t devices[], uint8_t *count);
#endif

#ifdef __cplusplus
}
#endif
