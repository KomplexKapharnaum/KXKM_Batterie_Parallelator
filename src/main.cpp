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
 * @file main.cpp
 * @brief Point d'entrée principal du programme.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.

  Ce programme permet de gérer les batteries en parallèle, de les surveiller
  et d'enregistrer des log sur une carte SD.

  Il utilise des INA pour mesurer la tension, le courant et la puissance
  des batteries, les GPIO I2C TCA pour contrôler les commutateurs de batterie
  et la bibliothèque SD pour enregistrer les données sur une carte SD.
  Il est possible :
  - d'activer un serveur web pour contrôler les batteries à distance.
  - de définir des seuils de tension, de courant et de commutation.
  - de définir un délai de reconnexion et un nombre de commutations avant déconnexion.
  - de définir un offset de différence de tension pour la déconnexion de la batterie.
  - de définir un temps entre chaque enregistrement de log sur SD.
  - de définir un courant de décharge maximal.
  - de définir un courant de charge maximal.
  - de connaitre la batterie avec la tension maximale, la tension minimale et la tension moyenne.
  - de connaitre l'état de la batterie, l'état de charge et de commuter la batterie.
  - de vérifier l'offset de différence de tension.
  - etc.

  Pour plus d'informations, consultez le fichier README.md. (qui n'existe pas encore ! :D)
 */

 // #define ENABLE_WEBSERVER // Ajouter cette ligne pour activer le serveur web

const int I2C_Speed = 100; // I2C speed in KHz

// value for battery check
const float set_min_voltage = 24000; // Battery undervoltage threshold in mV
const float set_max_voltage = 30000; // Battery overvoltage threshold in mV
const int set_max_current = 1000;    // Battery overcurrent threshold in mA
const int set_max_charge_current = 1000; // Battery charge current threshold in mA
const int set_max_discharge_current = 1000; // Battery discharge current threshold in mA
const int reconnect_delay = 10000; // delay to reconnect the battery in ms
const int nb_switch_on = 5; // number of switch on before infinite disconnect of the battery

// value for INA devices
const int max_INA_current = 50; // Max current in A for INA devices
const int INA_micro_ohm_shunt = 2000; // Max micro ohm for INA devices

extern const bool print_message; // Print message on serial monitor

const bool print_message = true; // Print message on serial monitor

const int log_time = 10; // Temps entre chaque enregistrement de log sur SD en secondes

#include "Batt_Parallelator_lib.h"
#include "SD_Logger.h"
#include "pin_mapppings.h"
#include "WebServerHandler.h"
#include <Arduino.h>

#ifdef ENABLE_WEBSERVER
const char* ssid = "your_SSID";
const char* password = "your_PASSWORD";
WebServerHandler webServerHandler(ssid, password); // Créer une instance de la classe WebServerHandler
#endif

INAHandler inaHandler; // Créer une instance de la classe INAHandler
TCAHandler tcaHandler; // Créer une instance de la classe TCAHandler
BATTParallelator BattParallelator; // Créer une instance de la classe BattParallelator
SDLogger sdLogger; // Créer une instance de la classe SDLogger
BatteryManager batteryManager; // Créer une instance de la classe BatteryManager

String logFilename = "datalog"; // Définir une variable globale pour le nom du fichier de log

const float voltage_offset = 0.5; // Offset de différence de tension en volts

void I2C_scanner() { // Trouver tous les appareils I2C
  byte count = 0;
  for (byte i = 8; i < 120; i++) {
    Wire.beginTransmission(i);
    delay(50);
    if (Wire.endTransmission() == 0) {
      Serial.print("Adresse trouvée : ");
      Serial.print(i, DEC);
      Serial.print(" (0x");
      Serial.print(i, HEX);
      Serial.println(")");
      count++;
      delay(1); // peut-être inutile ?
    } // fin de bonne réponse
  } // fin de la boucle for
  Serial.println("Terminé.");
  Serial.print("Trouvé ");
  Serial.print(count, DEC);
  Serial.println(" appareil(s).");
} // fin de I2C_scanner

void readINADataTask(void *pvParameters); // Ajouter cette ligne
void logDataTask(void *pvParameters); // Ajouter cette ligne
void checkBatteryVoltagesTask(void *pvParameters); // Ajouter cette ligne

void setup() {
  if (print_message) {
    Serial.begin(115200);
    I2C_scanner();
  }
  Wire.begin(SDA_pin, SCL_pin); // sda= GPIO_32 /scl= GPIO_33
  Wire.setClock(I2C_Speed * 1000); // définir I2C

  tcaHandler.begin();
  Serial.println("Configuration TCA terminée");

  inaHandler.set_max_voltage(set_max_voltage); // définir la tension maximale en mV
  inaHandler.set_min_voltage(set_min_voltage); // définir la tension minimale en mV
  inaHandler.set_max_current(set_max_current); // définir le courant maximal en mA
  inaHandler.set_max_charge_current(set_max_charge_current); // définir le courant de charge maximal en mA
  inaHandler.begin(max_INA_current, INA_micro_ohm_shunt); // Initialiser les INA
  if (print_message)
    Serial.println("Configuration INA terminée");

  CSVConfig csvConfig = {','}; // Définir la configuration CSV avec un séparateur de virgule
  sdLogger.begin(logFilename.c_str(), csvConfig); // Initialiser le logger SD avec le nom du fichier et la configuration CSV
  sdLogger.setLogTime(log_time); // Définir le temps entre chaque enregistrement en secondes
  if (print_message)
    Serial.println("Configuration du logger SD terminée");

  BattParallelator.set_max_voltage(set_max_voltage); // définir la tension maximale en mV
  BattParallelator.set_min_voltage(set_min_voltage); // définir la tension minimale en mV
  BattParallelator.set_max_current(set_max_current); // définir le courant maximal en mA
  BattParallelator.set_max_charge_current(set_max_charge_current); // définir le courant de charge maximal en mA
  BattParallelator.set_max_discharge_current(set_max_discharge_current); // définir le courant de décharge maximal en mA
  BattParallelator.set_reconnect_delay(reconnect_delay); // définir le délai de reconnexion en ms
  BattParallelator.set_nb_switch_on(nb_switch_on); // définir le nombre de commutations avant de déconnecter la batterie
  BattParallelator.set_max_diff_voltage(voltage_offset); // Définir l'offset de différence de tension
  if (print_message)
    Serial.println("Configuration de la gestion des batteries terminée");

  int Nb_TCA = tcaHandler.getNbTCA(); // récupérer le nombre de TCA
  int Nb_INA = inaHandler.getNbINA(); // récupérer le nombre de INA
  if (print_message) {
    Serial.println();
    Serial.print("trouvé : ");
    Serial.print(Nb_TCA);
    Serial.print(" INA et ");
    Serial.print(Nb_INA);
    Serial.println(" TCA");
  }
  if (Nb_TCA != Nb_INA / 4) { // Vérifier si le nombre de TCA et de INA est correct
    Serial.println("Erreur : Le nombre de TCA et de INA n'est pas correct");
    if (Nb_INA % 4 != 0) {
      Serial.println("Erreur : INA manquant");
    } else {
      Serial.println("Erreur : TCA manquant");
    }
  } else {
    Serial.println("Le nombre de TCA et de INA est correct");
  }

  // Démarrer la tâche de consommation en ampère-heure pour chaque batterie
  for (int i = 0; i < Nb_INA; i++) {
    batteryManager.startAmpereHourConsumptionTask(i, 1.0, 600); // 600 mesures par heure
  }

  // Créer une tâche pour lire les données des INA
  xTaskCreatePinnedToCore(
    readINADataTask, "ReadINADataTask", 4096, NULL, 1, NULL, 0);

  // Créer une tâche pour enregistrer les données sur la carte SD
  xTaskCreatePinnedToCore(
    logDataTask, "LogDataTask", 4096, NULL, 1, NULL, 1);

  // Créer une tâche pour vérifier la tension des batteries
  xTaskCreatePinnedToCore(
    checkBatteryVoltagesTask, "CheckBatteryVoltagesTask", 4096, NULL, 1, NULL, 1);

  #ifdef ENABLE_WEBSERVER
  webServerHandler.begin(); // Démarrer le serveur web
  #endif
}

void readINADataTask(void *pvParameters) {
  while (true) {
    int Nb_Batt = inaHandler.getNbINA(); // récupérer le nombre de INA
    float battery_voltages[Nb_Batt];
    for (int i = 0; i < Nb_Batt; i++) { // loop through all INA devices
      if (print_message) // read the INA device to serial monitor
        inaHandler.read(i, print_message);
    }
    vTaskDelay(500 / portTICK_PERIOD_MS); // Attendre 500 ms avant de lire à nouveau
  }
}

void logDataTask(void *pvParameters) {
  while (true) {
    if (sdLogger.shouldLog()) { // Vérifier si on doit enregistrer les données
      int Nb_Batt = inaHandler.getNbINA(); // récupérer le nombre de INA
      for (int i = 0; i < Nb_Batt; i++) {
        sdLogger.logData(millis(), i, inaHandler.read_volt(i),
                         inaHandler.read_current(i),
                         BattParallelator.check_battery_status(i),
                         batteryManager.getAmpereHourConsumption(i)); // Enregistrer les données sur la carte SD
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS); // Attendre 1 seconde avant de vérifier à nouveau
  }
}

void checkBatteryVoltagesTask(void *pvParameters) {
  const float voltageOffset = 0.5; // Définir l'offset de tension en volts

  while (true) {
    float maxVoltage = batteryManager.getMaxVoltage();
    float minVoltage = batteryManager.getMinVoltage();
    float averageVoltage = batteryManager.getAverageVoltage();

    if (print_message) {
      Serial.print("Max Voltage: ");
      Serial.println(maxVoltage);
      Serial.print("Min Voltage: ");
      Serial.println(minVoltage);
      Serial.print("Average Voltage: ");
      Serial.println(averageVoltage);
    }

    int Nb_Batt = inaHandler.getNbINA();
    for (int i = 0; i < Nb_Batt; i++) {
      if (!BattParallelator.check_voltage_offset(i, voltageOffset)) {
        BattParallelator.check_battery_connected_status(i); // Utiliser les valeurs pour vérifier l'état de la batterie
      }
    }

    vTaskDelay(10000 / portTICK_PERIOD_MS); // Attendre 10 secondes avant de vérifier à nouveau
  }
}

void loop() {
  #ifdef ENABLE_WEBSERVER
  webServerHandler.handleClient();
  #endif
  // Laisser les tâches FreeRTOS gérer les opérations
}