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
 * @file INA_NRJ_lib.cpp
 * @brief Implémentation de la classe INAHandler pour gérer les appareils INA.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
 */

#include "INA_NRJ_lib.h"

/**
 * @brief Constructeur de la classe INAHandler.
 */
INAHandler::INAHandler()
    : deviceNumber(UINT8_MAX), mux(portMUX_INITIALIZER_UNLOCKED), Nb_INA(0)
{
    memset((void *)sumBusMillVolts, 0, sizeof(sumBusMillVolts));
    memset((void *)sumBusMicroAmp, 0, sizeof(sumBusMicroAmp));
    memset((void *)readings, 0, sizeof(readings));
    memset((void *)INA_address_connected, 0, sizeof(INA_address_connected));
}

/**
 * @brief Initialiser les appareils INA.
 * @param amp Courant en ampères.
 * @param micro_ohm Résistance en micro-ohms.
 */
void INAHandler::begin(const uint8_t amp, const uint16_t micro_ohm)
{
    Serial.println();
    // Configuration de l'INA

    uint8_t devicesFound = 0;
    while (deviceNumber == UINT8_MAX)
    {
        devicesFound = INA.begin(amp, micro_ohm); // TODO ajuster les valeurs pour votre application spécifique
        Serial.print(F("Trouvé "));
        Serial.print(devicesFound);
        Serial.println(F(" appareils INA"));

        for (uint8_t i = 0; i < devicesFound; i++)
        {
            if (strcmp(INA.getDeviceName(i), "INA237") == 0)
            {
                Nb_INA++;
                INA_address_connected[Nb_INA - 1] = INA_ADDR[i];
                deviceNumber = i;

                Serial.println("Trouvé INA_" + String(deviceNumber) + " à l'adresse " + String(INA_ADDR[i]) + " est connecté");

                initialize_ina(deviceNumber);
            }
        }
        if (deviceNumber == UINT8_MAX && devicesFound == 0)
        {
            Serial.print(F("Aucun appareil trouvé. Attente de 5s et nouvelle tentative...\n"));
            delay(5000);
        }
    }

    INA.setAveraging(64);                  // Moyenne chaque lecture 64 fois
    INA.setBusConversion(8244);            // Temps de conversion maximum 8.244ms
    INA.setShuntConversion(8244);          // Temps de conversion maximum 8.244ms
    INA.setI2CSpeed(I2C_Speed * 10000);    // Définir la vitesse I2C
    INA.setMode(INA_MODE_CONTINUOUS_BOTH); // Mesure continue bus/shunt
}

/**
 * @brief Initialiser un appareil INA spécifique.
 * @param deviceNumber Numéro de l'appareil.
 */
void INAHandler::initialize_ina(const uint8_t deviceNumber)
{
    INA.reset(deviceNumber);                                            // Réinitialiser l'appareil aux paramètres par défaut
    const int shunt_overvoltage = max_current * 2;                      // Tension de shunt en mV
    INA.alertOnShuntOverVoltage(true, shunt_overvoltage, deviceNumber); // Alerte sur surtension de shunt
    INA.alertOnBusUnderVoltage(true, min_voltage / 1000, deviceNumber); // Alerte sur sous-tension de bus
    INA.alertOnBusOverVoltage(true, max_voltage / 1000, deviceNumber);  // Alerte sur surtension de bus
}

/**
 * @brief Lire les valeurs d'un appareil INA.
 * @param deviceNumber Numéro de l'appareil.
 * @param print_message true pour afficher les valeurs lues, false sinon.
 */
void INAHandler::read(const uint8_t deviceNumber, const bool print_message)
{
    float amps = ((float)INA.getBusMicroAmps(deviceNumber) / 100000);
    float volts = ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
    float power = ((float)volts * (float)amps);
    float power_read = ((float)INA.getBusMicroWatts(deviceNumber) / 1000);
    float shunt = ((float)INA.getShuntMicroVolts(deviceNumber) / 1000);
    if (print_message)
    {
        Serial.printf("Lecture INA %d\n", deviceNumber);
        Serial.print(F("\nTension de bus:   "));
        Serial.print((float)volts, 4);
        Serial.print(F("V\nCourant de bus:  "));
        Serial.print((float)amps, 4);
        Serial.print(F("A\nTension de shunt: "));
        Serial.print((float)shunt, 4);
        Serial.print(F("mV\nPuissance:        "));
        Serial.print((float)power, 4);
        Serial.print(F("W\nPuissance lue:    "));
        Serial.print((float)power_read, 4);
        Serial.print(F("\n\n"));
    }
}

/**
 * @brief Lire le courant d'un appareil INA.
 * @param deviceNumber Numéro de l'appareil.
 * @return Courant en ampères.
 */
float INAHandler::read_current(const uint8_t deviceNumber)
{
    return ((float)INA.getBusMicroAmps(deviceNumber) / 100000);
}

/**
 * @brief Lire la tension d'un appareil INA.
 * @param deviceNumber Numéro de l'appareil.
 * @return Tension en volts.
 */
float INAHandler::read_volt(const uint8_t deviceNumber)
{
    return ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
}

/**
 * @brief Lire la puissance d'un appareil INA.
 * @param deviceNumber Numéro de l'appareil.
 * @return Puissance en watts.
 */
float INAHandler::read_power(const uint8_t deviceNumber)
{
    float volts = ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
    float amps = ((float)INA.getBusMicroAmps(deviceNumber) / 10000);
    return ((float)volts * (float)amps);
}

/**
 * @brief Obtenir l'adresse d'un appareil INA.
 * @param deviceNumber Numéro de l'appareil.
 * @return Adresse de l'appareil.
 */
uint8_t INAHandler::getDeviceAddress(const uint8_t deviceNumber)
{
    return INA_ADDR[deviceNumber];
}

/**
 * @brief Obtenir le nombre d'appareils INA connectés.
 * @return Nombre d'appareils INA connectés.
 */
uint8_t INAHandler::getNbINA()
{
    return Nb_INA;
}

/**
 * @brief Définir la tension maximale.
 * @param voltage Tension maximale en volts.
 */
void INAHandler::set_max_voltage(const float_t voltage)
{
    max_voltage = voltage;
}

/**
 * @brief Définir la tension minimale.
 * @param voltage Tension minimale en volts.
 */
void INAHandler::set_min_voltage(const float_t voltage)
{
    min_voltage = voltage;
}

/**
 * @brief Définir le courant maximal.
 * @param current Courant maximal en ampères.
 */
void INAHandler::set_max_current(const float_t current)
{
    max_current = current;
}

/**
 * @brief Définir le courant de charge maximal.
 * @param current Courant de charge maximal en ampères.
 */
void INAHandler::set_max_charge_current(const float_t current)
{
    max_charge_current = current;
}

/**
 * @brief Définir la vitesse I2C.
 * @param speed Vitesse I2C en kHz.
 */
void INAHandler::setI2CSpeed(int speed)
{
    I2C_Speed = speed;
    INA.setI2CSpeed(I2C_Speed * 1000); // Définir la vitesse I2C
}
