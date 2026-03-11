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
#include "I2CMutex.h"
#include <DebugLogger.h>
extern DebugLogger debugLogger;

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
        // Configuration de l'INA

    uint8_t devicesFound = 0;
    while (deviceNumber == UINT8_MAX)
    {
        devicesFound = INA.begin(amp, micro_ohm); // TODO ajuster les valeurs pour votre application spécifique
        debugLogger.print(DebugLogger::INFO, "Trouvé " + String(devicesFound) + " appareils INA");

        for (uint8_t i = 0; i < devicesFound; i++)
        {
            if (strcmp(INA.getDeviceName(i), "INA237") == 0)
            {
                Nb_INA++;
                INA_address_connected[Nb_INA - 1] = INA_ADDR[i];
                deviceNumber = i;

                debugLogger.println(DebugLogger::INFO, "Trouvé INA_" + String(deviceNumber) + " à l'adresse " + String(INA_ADDR[i]) + " est connecté");

                initialize_ina(deviceNumber);
            }
        }
        if (deviceNumber == UINT8_MAX && devicesFound == 0)
        {
            debugLogger.println(DebugLogger::WARNING, "Aucun appareil trouvé. Attente de 5s et nouvelle tentative...");
            delay(5000);
        }
    }

    INA.setAveraging(64);                  // Moyenne chaque lecture 64 fois
    INA.setBusConversion(8244);            // Temps de conversion maximum 8.244ms
    INA.setShuntConversion(8244);          // Temps de conversion maximum 8.244ms
    INA.setI2CSpeed(I2C_Speed * 1000);     // Définir la vitesse I2C (100kHz)
    INA.setMode(INA_MODE_CONTINUOUS_BOTH); // Mesure continue bus/shunt
}

/**
 * @brief Initialiser un appareil INA spécifique.
 * @param deviceNumber Numéro de l'appareil.
 */
void INAHandler::initialize_ina(const uint8_t deviceNumber)
{
    INA.reset(deviceNumber);                                            // Réinitialiser l'appareil aux paramètres par défaut
    // Shunt overvoltage: max_current(mA) * shunt_resistance(2mOhm) / 1000 = mV
    // 2x safety margin for transients
    const int shunt_overvoltage_mV = (max_current * 2 * 2) / 1000;     // e.g. 1000mA * 2mOhm * 2x = 4mV
    INA.alertOnShuntOverVoltage(true, shunt_overvoltage_mV, deviceNumber);
    INA.alertOnBusUnderVoltage(true, min_voltage, deviceNumber);        // min_voltage already in mV (24000)
    INA.alertOnBusOverVoltage(true, max_voltage, deviceNumber);         // max_voltage already in mV (30000)
}

/**
 * @brief Lire les valeurs d'un appareil INA.
 * @param deviceNumber Numéro de l'appareil.
 */
void INAHandler::read(const uint8_t deviceNumber)
{
    float amps = ((float)INA.getBusMicroAmps(deviceNumber) / 100000);
    float volts = ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
    float power = ((float)volts * (float)amps);
    float power_read = ((float)INA.getBusMicroWatts(deviceNumber) / 1000);
    float shunt = ((float)INA.getShuntMicroVolts(deviceNumber) / 1000);
    /*
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
        */
}

/**
 * @brief Lire le courant d'un appareil INA.
 * @param deviceNumber Numéro de l'appareil.
 * @return Courant en ampères.
 */
float INAHandler::read_current(const uint8_t deviceNumber)
{
    I2CLockGuard lock;
    if (!lock.isAcquired()) return 0.0f;
    return ((float)INA.getBusMicroAmps(deviceNumber) / 100000);
}

/**
 * @brief Lire la tension d'un appareil INA.
 * @param deviceNumber Numéro de l'appareil.
 * @return Tension en volts.
 */
float INAHandler::read_volt(const uint8_t deviceNumber)
{
    I2CLockGuard lock;
    if (!lock.isAcquired()) return 0.0f;
    return ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
}

/**
 * @brief Lire la puissance d'un appareil INA.
 * @param deviceNumber Numéro de l'appareil.
 * @return Puissance en watts.
 */
float INAHandler::read_power(const uint8_t deviceNumber)
{
    I2CLockGuard lock;
    if (!lock.isAcquired()) return 0.0f;
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
