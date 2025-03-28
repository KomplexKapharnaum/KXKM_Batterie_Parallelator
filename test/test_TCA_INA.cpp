// test_TCA_INA.cpp

#include "INA_NRJ_lib.h"
#include "TCA_NRJ_lib.h"
#include <Arduino.h>
#include <DebugLogger.h>
#include <Wire.h>

// Déclarations globales
TCAHandler tcaHandler;
INAHandler inaHandler;
DebugLogger debugLogger;

bool tca_set = false; // Variable pour l'état de TCA
void setup() {
  Serial.begin(115200);
  const int SDA_pin = 32;       // GPIO_32
  const int SCL_pin = 33;       // GPIO_33
  Wire.begin(SDA_pin, SCL_pin); // sda= GPIO_32 /scl= GPIO_33

  // Initialisation du débogueur
  DebugLogger::DebugLevelInfo levels[] = {{"NONE", 1},    {"ERROR", 1},
                                          {"WARNING", 1}, {"INFO", 1},
                                          {"DEBUG", 1},   {"I2C", 1}};
  debugLogger.begin(levels, sizeof(levels) / sizeof(levels[0]));
  debugLogger.setDebugLevel(levels, sizeof(levels) / sizeof(levels[0]));

  // Initialisation des modules TCA et INA
  tcaHandler.begin();
  debugLogger.println(DebugLogger::INFO, "TCA Handler initialisé");

  inaHandler.begin(50, 2000); // Exemple : 50A max, 2000 µOhm shunt
  debugLogger.println(DebugLogger::INFO, "INA Handler initialisé");
}

void loop() {
  // Test des modules TCA
  debugLogger.println(DebugLogger::INFO, "Test des sorties TCA...");
  tca_set = !tca_set; // Inverser l'état de tca_set
  int Nb_TCA = tcaHandler.getNbTCA();
  for (int tca = 0; tca < Nb_TCA; tca++) {
    for (int pin = 0; pin < 16; pin++) {
      const int nb_cycles = 5; // Nombre de cycles d'allumage/extinction
      const int pwm_time = 50;   // Délai entre chaque cycle
      for (int i = 0; i < nb_cycles; i++) {
        tcaHandler.write(tca, pin, tca_set);
        delay(pwm_time);
        tcaHandler.write(tca, pin, !tca_set);
        delay(pwm_time);
      }
    }
  }

  // Test des modules INA
  debugLogger.println(DebugLogger::INFO, "Lecture des valeurs INA...");
  int Nb_INA = inaHandler.getNbINA();
  for (int i = 0; i < Nb_INA; i++) {
    float voltage = inaHandler.read_volt(i);
    float current = inaHandler.read_current(i);
    debugLogger.print(DebugLogger::INFO, "INA ");
    debugLogger.print(DebugLogger::INFO, String(i));
    debugLogger.print(DebugLogger::INFO, " Voltage: ");
    debugLogger.print(DebugLogger::INFO, String(voltage));
    debugLogger.print(DebugLogger::INFO, "V Current: ");
    debugLogger.println(DebugLogger::INFO, String(current) + "A");
  }
}
