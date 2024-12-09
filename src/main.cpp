#define I2C_Speed 50 // I2C speed in KHz
#define alert_bat_min_voltage 24000 // Battery undervoltage threshold in mV
#define alert_bat_max_voltage 30000 // Battery overvoltage threshold in mV
#define alert_bat_max_current 1    // Battery overcurrent threshold in A

#include <Arduino.h>
#include "pin_mapppings.h"
#include "INA_Func.h"
#include "TCA_Func.h"



void setup()
{
  Serial.begin(115200);
  Wire.begin(32, 33);              // sda= GPIO_32 /scl= GPIO_33
  Wire.setClock(I2C_Speed * 1000); // set I2C clock at 50 KHz
  // Find all I2C devices
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
      delay(1); // maybe unneeded?
    } // end of good response
  } // end of for loop
  Serial.println("Done.");
  Serial.print("Found ");
  Serial.print(count, DEC);
  Serial.println(" device(s).");
  setup_ina();
  Serial.println("INA setup done");
  TCA_init();
  Serial.println("TCA setup done");
  for (int i = 0; i < 16; i++)
  {
    TCA_write(TCA_num, i, 0);
  }
}

int value_pwm = 0;
void loop()
{
// for (int i = 0; i < Nb_INA; i++)
  for (int i = 0; i < Nb_INA; i++)
  {
    Serial.printf("Reading INA %d\n", i);
    read_INA(i);
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

    
        if (read_volt(i) < alert_bat_min_voltage / 1000)
        {
          Serial.println("Battery voltage is too low");
          TCA_write(TCA_num, i, 0); // switch off the battery
          TCA_write(TCA_num, i * 2 + 8, 1);
          TCA_write(TCA_num, i * 2 + 9, 0);
        }
        else if (read_volt(i) > alert_bat_max_voltage / 1000)
        {
          Serial.println("Battery voltage is too high");
          TCA_write(TCA_num, i, 0);
          TCA_write(TCA_num, i * 2 + 8, 1);
          TCA_write(TCA_num, i * 2 + 9, 0);
        }
        else if (read_current(i) > alert_bat_max_current)
        {
          Serial.println("Battery current is too high");
          TCA_write(TCA_num, i, 0);
          TCA_write(TCA_num, i * 2 + 8, 1);
          TCA_write(TCA_num, i * 2 + 9, 0);
        }
        else if (read_current(i) < -alert_bat_max_current)
        {
          Serial.println("Battery current is too high");
          TCA_write(TCA_num, i, 0);
          TCA_write(TCA_num, i * 2 + 8, 1);
          TCA_write(TCA_num, i * 2 + 9, 0);
        }
        else
        {
          Serial.println("Battery voltage and current are good");
          TCA_write(TCA_num, i, 1);
          TCA_write(TCA_num, i * 2 + 8, 0);
          TCA_write(TCA_num, i * 2 + 9, 1);
        }
        
  }
/*
  Serial.printf("Reading TCA %d\n", TCA_num);
  for (int j = 0; j < 16; j++)
  {
    Serial.printf("Pin %d: %d\n", j, TCA_read(TCA_num, j));
  }
*/

  delay(500);
} // of loop