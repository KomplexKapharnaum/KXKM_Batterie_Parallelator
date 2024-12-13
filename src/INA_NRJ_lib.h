/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file INA_NRJ_lib.h
 * @brief Déclaration de la classe INAHandler pour gérer les appareils INA.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
 */

#ifndef INA_NRJ_LIB_h
#define INA_NRJ_LIB_h

#include <INA.h> // Inclure la bibliothèque INA

/**
 * @class INAHandler
 * @brief Classe pour gérer les appareils INA.
 */
class INAHandler
{
public:
    INAHandler();
    void begin(const uint8_t amp, const uint16_t micro_ohm);
    void read(const uint8_t deviceNumber, bool print_message);
    float read_current(const uint8_t deviceNumber);
    float read_volt(const uint8_t deviceNumber);
    float read_power(const uint8_t deviceNumber);
    void set_max_voltage(const float_t voltage);
    void set_min_voltage(const float_t voltage);
    void set_max_current(const float_t current);
    void set_max_charge_current(const float_t current);
    uint8_t getDeviceAddress(const uint8_t deviceNumber);
    uint8_t getNbINA();
    void setI2CSpeed(int speed);

private:
    void initialize_ina(const uint8_t deviceNumber);
    const int INA_ADDR[16] = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F}; ///< Adresses INA
    INA_Class INA;                                                                                                             ///< Instanciation de la classe INA
    volatile uint8_t deviceNumber;                                                                                             ///< Numéro de l'appareil à utiliser dans l'exemple
    volatile uint64_t sumBusMillVolts[17];                                                                                     ///< Somme des lectures de tension de bus
    volatile int64_t sumBusMicroAmp[17];                                                                                       ///< Somme des lectures de courant de bus
    volatile uint8_t readings[17];                                                                                             ///< Nombre de mesures prises
    portMUX_TYPE mux;                                                                                                          ///< Variable de synchronisation
    byte INA_address_connected[8];                                                                                             // Adresse INA connectée
    uint8_t Nb_INA;                                                                                                            // Nombre d'INA connectés
    int max_voltage = 30000;                                                                                                   // Seuil de sous-tension de la batterie en mV;
    int min_voltage = 24000;                                                                                                   // Seuil de surtension de la batterie en mV
    int max_current = 1000;                                                                                                    // Seuil de surintensité de la batterie en mA
    int max_charge_current = 1000;                                                                                             // Seuil de courant de charge de la batterie en mA
    int I2C_Speed = 100; // Vitesse I2C en KHz
};

#endif // INA_NRJ_LIB_h
