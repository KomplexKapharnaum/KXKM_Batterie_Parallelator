/**************************************************************
** INA                                                       **
***************************************************************/

#include <INA.h> // Include the INA library

class INAHandler
{
public:
    INAHandler();
    void setup();
    void read(int deviceNumber, bool print_message);
    float read_current(int deviceNumber);
    float read_volt(int deviceNumber);
    float read_power(int deviceNumber);
    uint8_t getDeviceAddress(int deviceNumber);

private:
    void initialize_ina(int deviceNumber);

    const int INA_ADDR[16] = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F}; ///< INA addresses
    INA_Class INA;                                                                                                             ///< INA class instantiation
    volatile uint8_t deviceNumber;                                                                                             ///< Device Number to use in example
    volatile uint64_t sumBusMillVolts[17];                                                                                     ///< Sum of bus voltage readings
    volatile int64_t sumBusMicroAmp[17];                                                                                       ///< Sum of bus amperage readings
    volatile uint8_t readings[17];                                                                                             ///< Number of measurements taken
    portMUX_TYPE mux;                                                                                                          ///< Synchronization variable
    byte INA_address_connected[8];
    int Nb_INA;
};

INAHandler::INAHandler()
    : deviceNumber(UINT8_MAX), mux(portMUX_INITIALIZER_UNLOCKED), Nb_INA(0)
{
    memset((void*)sumBusMillVolts, 0, sizeof(sumBusMillVolts));
    memset((void*)sumBusMicroAmp, 0, sizeof(sumBusMicroAmp));
    memset((void*)readings, 0, sizeof(readings));
    memset((void*)INA_address_connected, 0, sizeof(INA_address_connected));
}

void INAHandler::setup()
{
    Serial.println();
    // INA setup

    uint8_t devicesFound = 0;
    while (deviceNumber == UINT8_MAX)
    {
        devicesFound = INA.begin(50, 2000); // +/- 1 Amps maximum for 0.2 Ohm resistor
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

    INA.setAveraging(64);                  // Average each reading 64 times
    INA.setBusConversion(8244);            // Maximum conversion time 8.244ms
    INA.setShuntConversion(8244);          // Maximum conversion time 8.244ms
    INA.setI2CSpeed(I2C_Speed * 10000);    // Set I2C speed
    INA.setMode(INA_MODE_CONTINUOUS_BOTH); // Bus/shunt measured continuously
}

void INAHandler::initialize_ina(int deviceNumber)
{
    INA.reset(deviceNumber);                                                      // Reset device to default settings
    const int shunt_overvoltage = alert_bat_max_current * 2;                      // Shunt voltage in mV
    INA.alertOnShuntOverVoltage(true, shunt_overvoltage, deviceNumber);           // Alert on shunt overvoltage
    INA.alertOnBusUnderVoltage(true, alert_bat_min_voltage / 1000, deviceNumber); // Alert on bus undervoltage
    INA.alertOnBusOverVoltage(true, alert_bat_max_voltage / 1000, deviceNumber);  // Alert on bus overvoltage
}

void INAHandler::read(int deviceNumber, bool print_message)
{
    float amps = ((float)INA.getBusMicroAmps(deviceNumber) / 100000);
    float volts = ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
    float power = ((float)volts * (float)amps);
    float power_read = ((float)INA.getBusMicroWatts(deviceNumber) / 1000);
    float shunt = ((float)INA.getShuntMicroVolts(deviceNumber) / 1000);
    if (print_message)
    {
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

float INAHandler::read_current(int deviceNumber)
{
    return ((float)INA.getBusMicroAmps(deviceNumber) / 100000);
}

float INAHandler::read_volt(int deviceNumber)
{
    return ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
}

float INAHandler::read_power(int deviceNumber)
{
    float volts = ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
    float amps = ((float)INA.getBusMicroAmps(deviceNumber) / 10000);
    return ((float)volts * (float)amps);
}

uint8_t INAHandler::getDeviceAddress(int deviceNumber) {
    return INA_ADDR[deviceNumber];
}
