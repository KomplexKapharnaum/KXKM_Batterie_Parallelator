#include "config.h"   // Tous les seuils et paramètres — à modifier ici uniquement

bool print_message = true;

long reconnect_time[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

int TCA_num = 0;
int OUT_num = 0;

#include <Arduino.h>
#include "pin_mappings.h"
#include "I2CMutex.h"
#include "INAHandler.h"
#include "TCAHandler.h"
#include "BatteryParallelator.h"
#include "BatteryManager.h"

// Global handler instances for I2C devices
INAHandler inaHandler;
TCAHandler tcaHandler;

// Global I2C mutex (declared in I2CMutex.h)
SemaphoreHandle_t i2cMutex = NULL;
volatile uint32_t g_i2cConsecutiveFailures = 0;

void setup()
{
  Serial.begin(115200);
  Wire.begin(32, 33);              // sda= GPIO_32 /scl= GPIO_33
  Wire.setClock(I2C_Speed * 1000); // set I2C clock at 50 KHz
  
  // CRIT-004: Initialize I2C mutex BEFORE any handlers or tasks
  i2cMutexInit();
  Serial.println("I2C mutex initialized");
  
  // Find all I2C devices (diagnostic I2C scan)
  byte count = 0;
  for (byte i = 8; i < 120; i++)
  {
    Wire.beginTransmission(i);
    delay(50);
    if (Wire.endTransmission() == 0)
    {
      Serial.print("Found address: ");
      Serial.print(i, DEC);
      Serial.print(" (0x");
      Serial.print(i, HEX);
      Serial.println(")");
      count++;
      delay(1);
    }
  }
  Serial.println("Done.");
  Serial.print("Found ");
  Serial.print(count, DEC);
  Serial.println(" device(s).");

  // CRIT-001: Replace setup_tca(), setup_ina(), check_INA_TCA_address()
  // with new handler-based initialization
  inaHandler.begin(0.5, 2000);  // 0.5 A max, 2000 micro-ohm shunt
  Serial.printf("INA setup done — found %d INA devices\n", inaHandler.getNbINA());
  
  tcaHandler.begin();
  Serial.println("TCA setup done");
  
  tcaHandler.check_INA_TCA_address();
  Serial.println("INA-TCA topology validation complete");

  // Initialize battery readings from detected INA devices
  for (int i = 0; i < inaHandler.getNbINA(); i++) 
  {
    // battery_voltage[i] = inaHandler.read_volt(i);  // Will be read in main loop
    // battery_current[i] = inaHandler.read_current(i);
    // battery_power[i] = inaHandler.read_power(i);
  }
}

void loop()
{
  // CRIT-002: Replace global Nb_INA with dynamic handler.getNbINA()
  int nbIna = inaHandler.getNbINA();
  
  for (int i = 0; i < nbIna; i++) // loop through all INA devices
  {
    battery_voltage[i] = inaHandler.read_volt(i);
    battery_current[i] = inaHandler.read_current(i);
    battery_power[i] = inaHandler.read_power(i);

    if (print_message)
      Serial.printf("Reading INA %d\n", i);
    // read_INA(i, print_message);  // OLD function — replace with handler
    
    if ((inaHandler.getDeviceAddress(i) >= 64) && (inaHandler.getDeviceAddress(i) <= 67))
    {
      TCA_num = 0;
      OUT_num = inaHandler.getDeviceAddress(i) - 64;
    }
    if ((inaHandler.getDeviceAddress(i) >= 68) && (inaHandler.getDeviceAddress(i) <= 71))
    {
      TCA_num = 1;
      OUT_num = inaHandler.getDeviceAddress(i) - 68;
    }
    if ((inaHandler.getDeviceAddress(i) >= 72) && (inaHandler.getDeviceAddress(i) <= 75))
    {
      TCA_num = 2;
      OUT_num = inaHandler.getDeviceAddress(i) - 72;
    }
    if ((inaHandler.getDeviceAddress(i) >= 76) && (inaHandler.getDeviceAddress(i) <= 79))
    {
      TCA_num = 3;
      OUT_num = inaHandler.getDeviceAddress(i) - 76;
    }

    /*
0 Batt switch 4
1 Batt switch 3
2 Batt switch 2
3 Batt switch 1
4 Alert 4
5 Alert 3
6 Alert 2
7 Alert 1
8 Red 1
9 Green 1
10 Red 2
11 Green 2
12 Red 3
13 Green 3
14 Red 4
15 Green 4
*/

    if (battery_voltage[i] < alert_bat_min_voltage / 1000.0f)
    {
      if (print_message)
        Serial.println("Battery voltage is too low");
      // switch_off_battery(TCA_num, OUT_num, i); // TODO: refactor to handler
    }
    else if (battery_voltage[i] > alert_bat_max_voltage / 1000.0f)
    {
      if (print_message)
        Serial.println("Battery voltage is too high");
      // switch_off_battery(TCA_num, OUT_num, i);
    }
    else if (battery_current[i] > alert_bat_max_current)
    {
      if (print_message)
        Serial.println("Battery current is too high");
      // switch_off_battery(TCA_num, OUT_num, i);
    }
    else if (battery_current[i] < -alert_bat_max_current)
    {
      if (print_message)
        Serial.println("Battery current is too high");
      // switch_off_battery(TCA_num, OUT_num, i);
    }
    else
    {
      // check_switch[i] = 0;
      // protection logic continues...
    }
  }
  delay(500);
}