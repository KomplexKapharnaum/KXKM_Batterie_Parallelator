/**************************************************************
** INA                                                       **
***************************************************************/

/* INA Adresses :
40 41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F
*/
const int INA_ADDR[16] = {0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F}; ///< INA addresses

#define INA_ALERT_PIN 2 // Pin-Change used for INA "ALERT" functionality
#define INA_ALERT_PIN_MASK 0x04
#define INA_ALERT_PIN_PORT PORTD
#define INA_ALERT_PIN_DDR DDRD
#define INA_ALERT_PIN_PIN PIND
#define INA_ALERT_PIN_PCICR PCICR
#define INA_ALERT_PIN_PCMSK PCMSK2
#define INA_ALERT_PIN_PCIF PCIF2
#define INA_ALERT_PIN_PCINT 2

#define INA_ALERT_PIN_ISR ISR(PCINT2_vect)

#include <INA.h> // Include the INA library

INA_Class INA;                                   ///< INA class instantiation
volatile uint8_t deviceNumber = UINT8_MAX;       ///< Device Number to use in example
volatile uint64_t sumBusMillVolts[17] = {};      ///< Sum of bus voltage readings
volatile int64_t sumBusMicroAmp[17] = {};        ///< Sum of bus amperage readings
volatile uint8_t readings[17];                   ///< Number of measurements taken
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED; ///< Synchronization variable

byte INA_address_connected[8] = {0, 0, 0, 0, 0, 0, 0, 0};
int Nb_INA = 0;


void setup_ina()
{
    Serial.println();
    // INA setup

    uint8_t devicesFound = 0;
    while (deviceNumber == UINT8_MAX) // Loop until we find the device
    {
        devicesFound = INA.begin(50, 2000); // +/- 1 Amps maximum for 0.2 Ohm resistor
        Serial.print(F("Found "));
        Serial.print(devicesFound);
        Serial.println(F(" INA devices"));

        for (uint8_t i = 0; i < devicesFound; i++)
        {
            /* Change the "INA226" in the following statement to whatever device you have attached and
               want to measure */
            if (strcmp(INA.getDeviceName(i), "INA237") == 0)
            {
                Nb_INA++;
                INA_address_connected[Nb_INA-1]=INA_ADDR[i];
                deviceNumber = i;
                
                Serial.println("Found INA_" + String(deviceNumber) + " at address " +String (INA_ADDR[i]) +" is connected");


                INA.reset(deviceNumber);                                                      // Reset device to default settings
                const int shunt_overvoltage = alert_bat_max_current * 2;                      // Shunt voltage in mV
                INA.alertOnShuntOverVoltage(true, shunt_overvoltage, deviceNumber);           // Alert on shunt overvoltage
                INA.alertOnBusUnderVoltage(true, alert_bat_min_voltage / 1000, deviceNumber); // Alert on bus undervoltage
                INA.alertOnBusOverVoltage(true, alert_bat_max_voltage / 1000, deviceNumber);  // Alert on bus overvoltage
                                                                                              // INA.alertOnConversion(false); // Make alert pin go low on finished conversion
                //  break;
            } // of if-then we have found an INA226
        } // of for-next loop through all devices found
        if (deviceNumber == UINT8_MAX && devicesFound == 0)
        {
            Serial.print(F("No device found. Waiting 5s and retrying...\n"));
            delay(5000);
        } // of if-then no device found
    } // of if-then no device found

    INA.setAveraging(64);                  // Average each reading 64 times
    INA.setBusConversion(8244);            // Maximum conversion time 8.244ms
    INA.setShuntConversion(8244);          // Maximum conversion time 8.244ms
    INA.setI2CSpeed(I2C_Speed * 10000);    // Set I2C speed
    INA.setMode(INA_MODE_CONTINUOUS_BOTH); // Bus/shunt measured continuously

}





void IRAM_ATTR InterruptHandler()
{
    /*!
      @brief Interrupt service routine for the INA pin
      @details Routine is called whenever the INA_ALERT_PIN changes value
    */
    portENTER_CRITICAL_ISR(&mux);
    //  sei();                                                 // Enable interrupts (for I2C calls)
    sumBusMillVolts[deviceNumber] += INA.getBusMilliVolts(deviceNumber); // Add current value to sum
    sumBusMicroAmp[deviceNumber] += INA.getBusMicroAmps(deviceNumber);   // Add current value to sum
    readings[deviceNumber]++;
    INA.waitForConversion(deviceNumber); // Wait for conv and reset interrupt
    //  cli();                               // Disable interrupts
    portEXIT_CRITICAL_ISR(&mux);
} // of ISR for handling interrupts



void read_INA(int deviceNumber,bool print_message) // Read the INA device
{

    float amps = ((float)INA.getBusMicroAmps(deviceNumber) / 100000);
    float volts = ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
    float power = ((float)volts * (float)amps);
    float power_read = ((float)INA.getBusMicroWatts(deviceNumber) / 1000);
    float shunt = ((float)INA.getShuntMicroVolts(deviceNumber) / 1000);
       if (print_message) {
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

float read_current(int deviceNumber) // Read the current in A
{
    return ((float)INA.getBusMicroAmps(deviceNumber) / 100000);
}

float read_volt(int deviceNumber) // Read the voltage in V
{
    return ((float)INA.getBusMilliVolts(deviceNumber)/ 1000);
}

float read_power(int deviceNumber) // calculate the power in W
{
    float volts = ((float)INA.getBusMilliVolts(deviceNumber) / 1000);
    float amps = ((float)INA.getBusMicroAmps(deviceNumber) / 10000);
    return ((float)volts * (float)amps);
}
