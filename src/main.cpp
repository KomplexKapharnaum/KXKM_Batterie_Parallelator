const int I2C_Speed = 50;                // I2C speed in KHz
const int alert_bat_min_voltage = 24000; // Battery undervoltage threshold in mV
const int alert_bat_max_voltage = 30000; // Battery overvoltage threshold in mV
const int alert_bat_max_current = 1;     // Battery overcurrent threshold in A
const bool print_message = true;
const int reconnect_delay = 10000;

#include <Arduino.h>
#include "pin_mapppings.h"
#include "INA_Func.h" 
#include "TCA_Func.h" 
#include "Batt_Compute.h"
#include "SD_Logger.h" 

INAHandler inaHandler;                 // Créer une instance de la classe INAHandler
TCAHandler tcaHandler;                 // Créer une instance de la classe TCAHandler
BattComputeHandler battComputeHandler; // Créer une instance de la classe BattComputeHandler
SDLogger sdLogger;                     // Créer une instance de la classe SDLogger

void setup()
{
  Serial.begin(115200);
  Wire.begin(SDA_pin, SCL_pin);              // sda= GPIO_32 /scl= GPIO_33
  Wire.setClock(I2C_Speed * 1000); // set I2C
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

  tcaHandler.setup();
  Serial.println("TCA setup done");

  inaHandler.setup();
  Serial.println("INA setup done");

  sdLogger.begin(); // Initialiser le logger SD

  int Nb_TCA = tcaHandler.getNbTCA(); // récupérer le nombre de TCA
  int Nb_INA = inaHandler.getNbINA(); // récupérer le nombre de INA

  Serial.println();
  Serial.print("found : ");
  Serial.print(Nb_TCA);
  Serial.print(" INA and ");
  Serial.print(Nb_INA);
  Serial.println(" TCA");

  if (Nb_TCA != Nb_INA / 4) // Vérifier si le nombre de TCA et de INA est correct
  {
    Serial.println("Error : Number of TCA and INA are not correct");
    if (Nb_INA % 4 != 0)
    {
      Serial.println("Error : Missing INA");
    }
    else
    {
      Serial.println("Error : Missing TCA");
    }
  }
  else
  {
    Serial.println("Number of TCA and INA are correct");
  }
}

void loop()
{
  int Nb_INA = inaHandler.getNbINA(); // récupérer le nombre de INA

  for (int i = 0; i < Nb_INA; i++) // loop through all INA devices
  {
    if (print_message)
      Serial.printf("Reading INA %d\n", i);
    inaHandler.read(i, print_message); // read the INA device
    battComputeHandler.check_battery(i); // check battery status and switch on/off if needed
    if (sdLogger.shouldLog()) // Vérifier si on doit enregistrer les données
    {
      float volt = inaHandler.read_volt(i); // Lire la tension
      float current = inaHandler.read_current(i); // Lire le courant
      bool switchState = battComputeHandler.check_battery_status(i); // Vérifier l'état de la batterie
      unsigned long time = millis();
      sdLogger.logData(volt, current, switchState, time); // Enregistrer les données sur la carte SD
    }
  } // of for-next loop through all INA devices
  delay(500);
} // of loop