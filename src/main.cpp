#include <Arduino.h>
#include <INA.h> // Include the INA library
#include "pin_mapppings.h"
/**************************************************************
** INA                                                       **
***************************************************************/

/* INA Adresses :
40 41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F
*/

INA_Class INA;                                   ///< INA class instantiation
volatile uint8_t deviceNumber = UINT8_MAX;       ///< Device Number to use in example
volatile uint64_t sumBusMillVolts[17] = {};           ///< Sum of bus voltage readings
volatile int64_t sumBusMicroAmp[17] = {};            ///< Sum of bus amperage readings
volatile uint8_t readings[17];                   ///< Number of measurements taken
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED; ///< Synchronization variable

void IRAM_ATTR InterruptHandler()
{
  /*!
    @brief Interrupt service routine for the INA pin
    @details Routine is called whenever the INA_ALERT_PIN changes value
  */
  portENTER_CRITICAL_ISR(&mux);
  //  sei();                                                 // Enable interrupts (for I2C calls)
  sumBusMillVolts[deviceNumber] += INA.getBusMilliVolts(deviceNumber); // Add current value to sum
  sumBusMicroAmp[deviceNumber] += INA.getBusMicroAmps(deviceNumber);  // Add current value to sum
  readings[deviceNumber]++;
  INA.waitForConversion(deviceNumber); // Wait for conv and reset interrupt
  //  cli();                               // Disable interrupts
  portEXIT_CRITICAL_ISR(&mux);
} // of ISR for handling interrupts

/**************************************************************
** TCA                                                       **
***************************************************************/

#include "TCA9555.h"

const int TCA_address[8] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27};

TCA9535 TCA_0(TCA_address[0]);
TCA9535 TCA_1(TCA_address[1]);
TCA9535 TCA_2(TCA_address[2]);
TCA9535 TCA_3(TCA_address[3]);
TCA9535 TCA_4(TCA_address[4]);
TCA9535 TCA_5(TCA_address[5]);
TCA9535 TCA_6(TCA_address[6]);
TCA9535 TCA_7(TCA_address[7]);

void setup()
{
  Wire.begin();

  // Find all I2C devices
  byte count = 0;
  for (byte i = 1; i < 120; i++)
  {
    Wire.beginTransmission(i);
    if (Wire.endTransmission() == 0)
    {
      Serial.print("Found address: ");
      Serial.print(i, DEC);
      Serial.print(" (0x");
      Serial.print(i, HEX);
      Serial.println(")");
      count++;
      delay(1); // maybe unneeded?
    } // end of good response
  } // end of for loop
  Serial.println("Done.");
  Serial.print("Found ");
  Serial.print(count, DEC);
  Serial.println(" device(s).");

  // Test the TCA9535 as plugged and start the class
  for (int i = 0; i < 8; i++)
  {
    Wire.beginTransmission(TCA_address[i]);
    if (Wire.endTransmission() == 0)
    {
      if (i == 0)
      {
        TCA_0.begin();
        for (int i = 0; i < 3; i++)
        {
          TCA_0.pinMode1(i, OUTPUT);
          TCA_0.write1(i, LOW);
        }
        for (int i = 4; i < 10; i++)
        {
          TCA_0.pinMode1(i, INPUT);
          TCA_0.write1(i, LOW);
        }
        for (int i = 10; i < 16; i++)
        {
          TCA_0.pinMode1(i, OUTPUT);
          TCA_0.write1(i, LOW);
        }
      }
      if (i == 1)
      {
        TCA_1.begin();
        for (int i = 0; i < 3; i++)
        {
          TCA_1.pinMode1(i, OUTPUT);
          TCA_1.write1(i, LOW);
        }
        for (int i = 4; i < 10; i++)
        {
          TCA_1.pinMode1(i, INPUT);
        }
        for (int i = 10; i < 16; i++)
        {
          TCA_1.pinMode1(i, OUTPUT);
          TCA_1.write1(i, LOW);
        }
      }
      if (i == 2)
      {
        TCA_2.begin();
        for (int i = 0; i < 3; i++)
        {
          TCA_2.pinMode1(i, OUTPUT);
          TCA_2.write1(i, LOW);
        }
        for (int i = 4; i < 10; i++)
        {
          TCA_2.pinMode1(i, INPUT);
        }
        for (int i = 10; i < 16; i++)
        {
          TCA_2.pinMode1(i, OUTPUT);
          TCA_2.write1(i, LOW);
        }
      }
      if (i == 3)
      {
        TCA_3.begin();
        for (int i = 0; i < 3; i++)
        {
          TCA_3.pinMode1(i, OUTPUT);
          TCA_3.write1(i, LOW);
        }
        for (int i = 4; i < 10; i++)
        {
          TCA_3.pinMode1(i, INPUT);
        }
        for (int i = 10; i < 16; i++)
        {
          TCA_3.pinMode1(i, OUTPUT);
          TCA_3.write1(i, LOW);
        }
      }
      if (i == 4)
      {
        TCA_4.begin();
        for (int i = 0; i < 3; i++)
        {
          TCA_4.pinMode1(i, OUTPUT);
          TCA_4.write1(i, LOW);
        }
        for (int i = 4; i < 10; i++)
        {
          TCA_4.pinMode1(i, INPUT);
        }
        for (int i = 10; i < 16; i++)
        {
          TCA_4.pinMode1(i, OUTPUT);
          TCA_4.write1(i, LOW);
        }
      }
      if (i == 5)
      {
        TCA_5.begin();
        for (int i = 0; i < 3; i++)
        {
          TCA_5.pinMode1(i, OUTPUT);
          TCA_5.write1(i, LOW);
        }
        for (int i = 4; i < 10; i++)
        {
          TCA_5.pinMode1(i, INPUT);
        }
        for (int i = 10; i < 16; i++)
        {
          TCA_5.pinMode1(i, OUTPUT);
          TCA_5.write1(i, LOW);
        }
      }
      if (i == 6)
      {
        TCA_6.begin();
        for (int i = 0; i < 3; i++)
        {
          TCA_6.pinMode1(i, OUTPUT);
          TCA_6.write1(i, LOW);
        }
        for (int i = 4; i < 10; i++)
        {
          TCA_6.pinMode1(i, INPUT);
        }
        for (int i = 10; i < 16; i++)
        {
          TCA_6.pinMode1(i, OUTPUT);
          TCA_6.write1(i, LOW);
        }
      }
      if (i == 7)
      {
        TCA_7.begin();
        for (int i = 0; i < 3; i++)
        {
          TCA_7.pinMode1(i, OUTPUT);
          TCA_7.write1(i, LOW);
        }
        for (int i = 4; i < 10; i++)
        {
          TCA_7.pinMode1(i, INPUT);
        }
        for (int i = 10; i < 16; i++)
        {
          TCA_7.pinMode1(i, OUTPUT);
          TCA_7.write1(i, LOW);
        }
      }
    }
  }

  // INA setup
  Serial.print(F("\n\nBackground INA Read V1.0.1\n"));
  uint8_t devicesFound = 0;
  while (deviceNumber == UINT8_MAX) // Loop until we find the first device
  {
    devicesFound = INA.begin(1, 100000); // +/- 1 Amps maximum for 0.1 Ohm resistor // TODO : check this
    Serial.println(INA.getDeviceName(devicesFound - 1));
    for (uint8_t i = 0; i < devicesFound; i++)
    {
      /* Change the "INA226" in the following statement to whatever device you have attached and
         want to measure */
      if (strcmp(INA.getDeviceName(i), "INA237") == 0)
      {
        deviceNumber = i;
        INA.reset(deviceNumber); // Reset device to default settings
        break;
      } // of if-then we have found an INA226
    } // of for-next loop through all devices found
    if (deviceNumber == UINT8_MAX)
    {
      Serial.print(F("No INA found. Waiting 5s and retrying...\n"));
      delay(5000);
    } // of if-then no INA226 found
  } // of if-then no device found
  Serial.print(F("Found INA at device number "));
  Serial.println(deviceNumber);
  Serial.println();
  INA.setAveraging(64);                  // Average each reading 64 times
  INA.setBusConversion(8244);            // Maximum conversion time 8.244ms
  INA.setShuntConversion(8244);          // Maximum conversion time 8.244ms
  INA.setMode(INA_MODE_CONTINUOUS_BOTH); // Bus/shunt measured continuously
  INA.alertOnConversion(true);           // Make alert pin go low on finish
  Serial.begin(115200);
}

void loop()
{
  static long lastMillis = millis(); // Store the last time we printed something
  if (readings[deviceNumber] >= 10)
  {
    Serial.print(F("Averaging readings taken over "));
    Serial.print((float)(millis() - lastMillis) / 1000, 2);
    Serial.print(F(" seconds.\nBus voltage:   "));
    Serial.print((float)sumBusMillVolts[deviceNumber] / readings[deviceNumber] / 1000.0, 4);
    Serial.print(F("V\nBus amperage:  "));
    Serial.print((float)sumBusMicroAmp[deviceNumber] / readings[deviceNumber] / 1000.0, 4);
    Serial.print(F("mA\n\n"));
    lastMillis = millis();
    //    cli();  // Disable interrupts to reset values
    readings[deviceNumber] = 0;
    sumBusMillVolts[deviceNumber] = 0;
    sumBusMicroAmp[deviceNumber] = 0;
    //    sei();  // Enable interrupts again
  }
}

// TCA functions
bool TCA_read(int TCA_num, int pin) // TCA_num is the TCA number, pin is the pin number return read value
{
  bool val;
  if (TCA_num == 0)
    val = TCA_0.read1(pin);
  if (TCA_num == 1)
    val = TCA_1.read1(pin);
  if (TCA_num == 2)
    val = TCA_2.read1(pin);
  if (TCA_num == 3)
    val = TCA_3.read1(pin);
  if (TCA_num == 4)
    val = TCA_4.read1(pin);
  if (TCA_num == 5)
    val = TCA_5.read1(pin);
  if (TCA_num == 6)
    val = TCA_6.read1(pin);
  if (TCA_num == 7)
    val = TCA_7.read1(pin);
  if (TCA_num > 7)
  {
    return 0;
  }
  Serial.print(val);
  Serial.print('\t');
  return val;
}

void TCA_write(int TCA_num, int pin, int value) // write value to pin
{
  if (TCA_num == 0)
    TCA_0.write1(TCA_num, value);
  if (TCA_num == 1)
    TCA_1.write1(TCA_num, value);
  if (TCA_num == 2)
    TCA_2.write1(TCA_num, value);
  if (TCA_num == 3)
    TCA_3.write1(TCA_num, value);
  if (TCA_num == 4)
    TCA_4.write1(TCA_num, value);
  if (TCA_num == 5)
    TCA_5.write1(TCA_num, value);
  if (TCA_num == 6)
    TCA_6.write1(TCA_num, value);
  if (TCA_num == 7)
    TCA_7.write1(TCA_num, value);
  if (TCA_num > 7)
  {
    return;
  }
}