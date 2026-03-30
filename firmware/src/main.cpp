#include "config.h"   // Tous les seuils et paramètres — à modifier ici uniquement

bool print_message = true;

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
BATTParallelator BattParallelator;
BatteryManager batteryManager;

// Global I2C mutex (declared in I2CMutex.h)
SemaphoreHandle_t i2cMutex = NULL;
static bool g_topologyValid = true;

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

  // Initialisation handlers capteurs/actuateurs
  inaHandler.begin(0.5, 2000);  // 0.5 A max, shunt 2000 micro-ohm
  Serial.printf("INA setup done — found %d INA devices\n", inaHandler.getNbINA());
  
  tcaHandler.begin();
  Serial.println("TCA setup done");
  
  tcaHandler.check_INA_TCA_address();
  Serial.println("INA-TCA topology validation complete");

  // Config protection — pass mV/mA directly (mem_set_* stores mV/mA)
  BattParallelator.set_min_voltage(alert_bat_min_voltage);       // 24000 mV
  BattParallelator.set_max_voltage(alert_bat_max_voltage);       // 30000 mV
  BattParallelator.set_max_current(alert_bat_max_current * 1000.0f); // 10A -> 10000 mA
  BattParallelator.set_max_diff_voltage(voltage_diff * 1000.0f); // 1V -> 1000 mV
  BattParallelator.set_reconnect_delay(reconnect_delay);
  BattParallelator.set_nb_switch_on(Nb_switch_max);

  // Validation topologie: 1 TCA pour 4 INA
  const uint8_t nbIna = inaHandler.getNbINA();
  const uint8_t nbTca = tcaHandler.getNbTCA();
  g_topologyValid = (nbIna > 0) && (nbTca > 0) && ((nbTca * 4) == nbIna);
  if (!g_topologyValid) {
    Serial.printf("Topology mismatch: Nb_INA=%u Nb_TCA=%u (attendu Nb_TCA*4 == Nb_INA)\n",
                  nbIna, nbTca);
    // Fail-safe: ne pas autoriser la logique de reconnexion automatique
  }

  for (int i = 0; i < nbIna; i++) {
    BattParallelator.check_battery_connected_status(i);
  }
}

void loop()
{
  const int nbIna = inaHandler.getNbINA();
  if (nbIna <= 0) {
    delay(500);
    return;
  }

  if (!g_topologyValid) {
    // Fail-safe: topologie invalide, forcer OFF pour éviter couplage dangereux
    for (int i = 0; i < nbIna; i++) {
      BattParallelator.switch_battery(i, false);
    }
    delay(500);
    return;
  }

  for (int i = 0; i < nbIna; i++) {
    BattParallelator.check_battery_connected_status(i);
  }

  delay(500);
}