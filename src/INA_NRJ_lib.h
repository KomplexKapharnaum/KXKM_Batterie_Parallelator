#ifndef INA_NRJ_LIB_h
#define INA_NRJ_LIB_h

#include <INA.h> // Include the INA library

class INAHandler
{
public:
    INAHandler();
    void begin(const uint8_t amp, const uint8_t micro_ohm);
    void read(const uint8_t deviceNumber, bool print_message);
    float read_current(const uint8_t deviceNumber);
    float read_volt(const uint8_t deviceNumber);
    float read_power(const uint8_t deviceNumber);
    void set_max_voltage(const float_t voltage);
    void set_min_voltage(const float_t voltage);
    void set_max_current(const float_t current);
    void set_max_charge_current(const float_t current);
    uint8_t getDeviceAddress(const uint8_t deviceNumber);
    uint8_t getNbINA(); // Ajout de la méthode getNbINA

private:
    void initialize_ina(const uint8_t deviceNumber);
    const int INA_ADDR[16] = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F}; ///< INA addresses
    INA_Class INA;                                                                                                             ///< INA class instantiation
    volatile uint8_t deviceNumber;                                                                                             ///< Device Number to use in example
    volatile uint64_t sumBusMillVolts[17];                                                                                     ///< Sum of bus voltage readings
    volatile int64_t sumBusMicroAmp[17];                                                                                       ///< Sum of bus amperage readings
    volatile uint8_t readings[17];                                                                                             ///< Number of measurements taken
    portMUX_TYPE mux;                                                                                                          ///< Synchronization variable
    byte INA_address_connected[8];                                                                                             // INA address connected
    uint8_t Nb_INA;                                                                                                            // Number of INA connected
    int max_voltage = 30000;                                                                                                   // Battery undervoltage threshold in mV;
    int min_voltage = 24000;                                                                                                   // Battery overvoltage threshold in mV
    int max_current = 1000;                                                                                                    // Battery overcurrent threshold in mA
    int max_charge_current = 1000;                                                                                             // Battery charge current threshold in mA
};

INAHandler::INAHandler()
    : deviceNumber(UINT8_MAX), mux(portMUX_INITIALIZER_UNLOCKED), Nb_INA(0)
{
    memset((void *)sumBusMillVolts, 0, sizeof(sumBusMillVolts));
    memset((void *)sumBusMicroAmp, 0, sizeof(sumBusMicroAmp));
    memset((void *)readings, 0, sizeof(readings));
    memset((void *)INA_address_connected, 0, sizeof(INA_address_connected));
}

/**
 * @brief Initialise l'INA avec les paramètres spécifiés.
 * 
 * @param amp Courant en ampères.
 * @param micro_ohm Résistance en micro-ohms.
 */
void INAHandler::begin(const uint8_t amp, const uint8_t micro_ohm)
{
    Serial.println();
    // Configuration de l'INA

    uint8_t devicesFound = 0;
    while (deviceNumber == UINT8_MAX)
    {
        devicesFound = INA.begin(amp, micro_ohm); // TODO ajuster les valeurs pour votre application spécifique
        Serial.print(F("Found "));
        Serial.print(devicesFound);
        Serial.println(F(" INA devices"));

        for (uint8_t i = 0; i < devicesFound; i++)
        {
            if (strcmp(INA.getDeviceName(i), "INA237") == 0)
            {
                Nb_INA++;
                INA_address_connected[Nb_INA - 1] = INA_ADDR[i];
                deviceNumber = i;

                Serial.println("Found INA_" + String(deviceNumber) + " at address " + String(INA_ADDR[i]) + " is connected");

                initialize_ina(deviceNumber);
            }
        }
        if (deviceNumber == UINT8_MAX && devicesFound == 0)
        {
            Serial.print(F("No device found. Waiting 5s and retrying...\n"));
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
 * @brief Initialise l'INA pour un appareil spécifique.
 * 
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
 * @brief Lit les valeurs de l'INA et affiche les messages si nécessaire.
 * 
 * @param deviceNumber Numéro de l'appareil.
 * @param print_message Indicateur pour afficher les messages.
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
        Serial.printf("Reading INA %d\n", deviceNumber);
        Serial.print(F("\nBus voltage:   "));
        Serial.print((float)volts, 4);
        Serial.print(F("V\nBus amperage:  "));
        Serial.print((float)amps, 4);
        Serial.print(F("A\nShunt voltage: "));
        Serial.print((float)shunt, 4);
        Serial.print(F("mV\nPower:        "));
        Serial.print((float)power, 4);
        Serial.print(F("W\nPower read:    "));
        Serial.print((float)power_read, 4);
        Serial.print(F("\n\n"));
    }
}

/**
 * @brief Lit le courant de l'INA.
 * 
 * @param deviceNumber Numéro de l'appareil.
 * @return float Courant en ampères.
 */
float INAHandler::read_current(const uint8_t deviceNumber)
{
    return ((float)INA.getBusMicroAmps(deviceNumber) / 100000);
}

/**
 * @brief Lit la tension de l'INA.
 * 
 * @param deviceNumber Numéro de l'appareil.
 * @return float Tension en volts.
 */
float INAHandler::read_volt(const uint8_t deviceNumber)
{
    return ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
}

/**
 * @brief Lit la puissance de l'INA.
 * 
 * @param deviceNumber Numéro de l'appareil.
 * @return float Puissance en watts.
 */
float INAHandler::read_power(const uint8_t deviceNumber)
{
    float volts = ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
    float amps = ((float)INA.getBusMicroAmps(deviceNumber) / 10000);
    return ((float)volts * (float)amps);
}

/**
 * @brief Obtient l'adresse de l'appareil.
 * 
 * @param deviceNumber Numéro de l'appareil.
 * @return uint8_t Adresse de l'appareil.
 */
uint8_t INAHandler::getDeviceAddress(const uint8_t deviceNumber)
{
    return INA_ADDR[deviceNumber];
}

/**
 * @brief Obtient le nombre d'INA connectés.
 * 
 * @return uint8_t Nombre d'INA connectés.
 */
uint8_t INAHandler::getNbINA()
{
    return Nb_INA;
}

/**
 * @brief Définit la tension maximale en mV.
 * 
 * @param voltage Tension maximale en mV.
 */
void INAHandler::set_max_voltage(const float_t voltage)
{
    max_voltage = voltage;
}

/**
 * @brief Définit la tension minimale en mV.
 * 
 * @param voltage Tension minimale en mV.
 */
void INAHandler::set_min_voltage(const float_t voltage)
{
    min_voltage = voltage;
}

/**
 * @brief Définit le courant maximal en mA.
 * 
 * @param current Courant maximal en mA.
 */
void INAHandler::set_max_current(const float_t current)
{
    max_current = current;
}

/**
 * @brief Définit le courant de charge maximal en mA.
 * 
 * @param current Courant de charge maximal en mA.
 */
void INAHandler::set_max_charge_current(const float_t current)
{
    max_charge_current = current;
}

#endif // INA_NRJ_LIB_h
