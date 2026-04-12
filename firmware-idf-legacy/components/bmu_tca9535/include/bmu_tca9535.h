#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * TCA9535 register map (TI SCPS209)
 * -------------------------------------------------------------------------- */
#define TCA9535_REG_INPUT_PORT0     0x00
#define TCA9535_REG_INPUT_PORT1     0x01
#define TCA9535_REG_OUTPUT_PORT0    0x02
#define TCA9535_REG_OUTPUT_PORT1    0x03
#define TCA9535_REG_POLARITY_INV0   0x04
#define TCA9535_REG_POLARITY_INV1   0x05
#define TCA9535_REG_CONFIG_PORT0    0x06
#define TCA9535_REG_CONFIG_PORT1    0x07

/* --------------------------------------------------------------------------
 * I2C address range : 0x20 – 0x27 (base 0b0100_A2A1A0)
 * -------------------------------------------------------------------------- */
#define TCA9535_BASE_ADDR           0x20
#define TCA9535_MAX_DEVICES         8

/* --------------------------------------------------------------------------
 * BMU PCB v2 port directions
 *   Port 0 : P0.7-P0.4 = alert inputs, P0.3-P0.0 = battery switch outputs
 *   Port 1 : P1.7-P1.0 = LED outputs (all output)
 * -------------------------------------------------------------------------- */
#define BMU_TCA_CONFIG_PORT0        0xF0   /* bits 7-4 input, bits 3-0 output */
#define BMU_TCA_CONFIG_PORT1        0x00   /* all output                       */

/* Nombre de voies batterie par TCA9535 */
#define BMU_TCA_CHANNELS_PER_DEVICE 4

/* --------------------------------------------------------------------------
 * Handle — un par TCA9535 detecte sur le bus
 * -------------------------------------------------------------------------- */
typedef struct {
    i2c_master_dev_handle_t dev;    /* handle I2C du device                   */
    uint8_t                 addr;   /* adresse I2C (0x20-0x27)                */
    uint8_t                 out_p0; /* cache local du registre Output Port 0  */
    uint8_t                 out_p1; /* cache local du registre Output Port 1  */
} bmu_tca9535_handle_t;

/* --------------------------------------------------------------------------
 * Discovery & initialisation
 * -------------------------------------------------------------------------- */

/**
 * @brief Decouvre et initialise tous les TCA9535 presents sur le bus.
 *
 * Pour chaque adresse 0x20-0x27 : ajoute le device I2C, configure les
 * directions (Config Port 0/1) et force toutes les sorties a LOW
 * (switches OFF, LEDs OFF).
 *
 * @param bus         Handle du bus I2C maitre
 * @param handles     Tableau de sortie (au moins TCA9535_MAX_DEVICES elements)
 * @param max_devices Taille du tableau handles
 * @param found       [out] nombre de TCA9535 detectes
 * @return ESP_OK si au moins un device trouve, ESP_ERR_NOT_FOUND sinon
 */
esp_err_t bmu_tca9535_scan_init(i2c_master_bus_handle_t bus,
                                bmu_tca9535_handle_t   *handles,
                                uint8_t                 max_devices,
                                uint8_t                *found);

/**
 * @brief Initialise un seul TCA9535 a une adresse donnee.
 *
 * Configure les directions et force les sorties a LOW.
 *
 * @param bus     Handle du bus I2C maitre
 * @param addr    Adresse I2C (0x20-0x27)
 * @param handle  [out] handle initialise
 * @return ESP_OK en cas de succes
 */
esp_err_t bmu_tca9535_init(i2c_master_bus_handle_t bus,
                           uint8_t                 addr,
                           bmu_tca9535_handle_t   *handle);

/* --------------------------------------------------------------------------
 * Commandes batterie
 * -------------------------------------------------------------------------- */

/**
 * @brief Active ou desactive le switch MOSFET d'une voie batterie.
 *
 * Mapping PCB (numeros inverses) :
 *   channel 0 (bat1) → P0.3
 *   channel 1 (bat2) → P0.2
 *   channel 2 (bat3) → P0.1
 *   channel 3 (bat4) → P0.0
 *
 * @param handle  Handle du TCA9535
 * @param channel Voie 0-3
 * @param on      true = switch ON (HIGH), false = switch OFF (LOW)
 * @return ESP_OK en cas de succes, ESP_ERR_INVALID_ARG si channel > 3
 */
esp_err_t bmu_tca9535_switch_battery(bmu_tca9535_handle_t *handle,
                                     uint8_t               channel,
                                     bool                  on);

/**
 * @brief Controle les LEDs rouge et verte d'une voie batterie.
 *
 * Mapping PCB :
 *   channel 0 (bat1) : red=P1.0, green=P1.1
 *   channel 1 (bat2) : red=P1.2, green=P1.3
 *   channel 2 (bat3) : red=P1.4, green=P1.5
 *   channel 3 (bat4) : red=P1.6, green=P1.7
 *
 * @param handle  Handle du TCA9535
 * @param channel Voie 0-3
 * @param red     true = LED rouge ON
 * @param green   true = LED verte ON
 * @return ESP_OK en cas de succes, ESP_ERR_INVALID_ARG si channel > 3
 */
esp_err_t bmu_tca9535_set_led(bmu_tca9535_handle_t *handle,
                              uint8_t               channel,
                              bool                  red,
                              bool                  green);

/**
 * @brief Lit l'etat de l'entree alerte d'une voie batterie.
 *
 * Mapping PCB (numeros inverses) :
 *   channel 0 (bat1) → P0.7
 *   channel 1 (bat2) → P0.6
 *   channel 2 (bat3) → P0.5
 *   channel 3 (bat4) → P0.4
 *
 * @param handle  Handle du TCA9535
 * @param channel Voie 0-3
 * @param alert   [out] true si alerte active (pin LOW)
 * @return ESP_OK en cas de succes, ESP_ERR_INVALID_ARG si channel > 3
 */
esp_err_t bmu_tca9535_read_alert(bmu_tca9535_handle_t *handle,
                                 uint8_t               channel,
                                 bool                  *alert);

/**
 * @brief Coupe tous les switches et eteint toutes les LEDs.
 *
 * @param handle  Handle du TCA9535
 * @return ESP_OK en cas de succes
 */
esp_err_t bmu_tca9535_all_off(bmu_tca9535_handle_t *handle);

/* ── Bit-bang bus variants ────────────────────────────────────────── */
#ifdef CONFIG_BMU_I2C_BB_ENABLED
#include "bmu_i2c_bitbang.h"

typedef struct {
    bmu_i2c_bb_handle_t bb;
    uint8_t             addr;
    uint8_t             out_p0;
    uint8_t             out_p1;
} bmu_tca9535_bb_handle_t;

esp_err_t bmu_tca9535_bb_scan_init(bmu_i2c_bb_handle_t bb,
                                    bmu_tca9535_bb_handle_t *handles,
                                    uint8_t max_devices, uint8_t *found);
esp_err_t bmu_tca9535_bb_switch_battery(bmu_tca9535_bb_handle_t *h,
                                         uint8_t channel, bool on);
esp_err_t bmu_tca9535_bb_set_led(bmu_tca9535_bb_handle_t *h,
                                  uint8_t channel, bool red, bool green);
esp_err_t bmu_tca9535_bb_all_off(bmu_tca9535_bb_handle_t *h);
#endif

#ifdef __cplusplus
}
#endif
