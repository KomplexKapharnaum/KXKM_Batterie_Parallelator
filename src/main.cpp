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

/*
API TOKEN GRAFANA =
BpeukQisC9YQzckyWpFNLmjjoR-YhlHxE0upYKJqeuVzvbTzdz_mE58vWRBnEXa7ZufKI5JFrQ3nm6VeV89GiA==
*/
/**
 * @file main.cpp
 * @brief Point d'entrée principal du programme.
 * @details Créé par Clément Saillant @electron-rare pour Komplex Kapharnum
 assisté par IA.
 * @details fonction de calcul par Nicolas Guichard @nicoguich
 * Ce programme permet de gérer les batteries  en parallèle, de les surveiller
 * et d'enregistrer des log sur une carte SD.
 *
 * Il utilise des puce INA237 pour mesurer la tension, le courant et la
 * puissance des batteries, des puces GPIO I2C TCAxxxx pour contrôler les
 * commutateurs de batterie et les leds et la bibliothèque SD pour enregistrer
 * les données sur une carte SD. Il est possible :
 * - d'activer un serveur web pour contrôler les batteries à distance.
 * - de définir des seuils de tension, de courant et de commutation.
 * - de définir un délai de reconnexion et un nombre de commutations avant
 * déconnexion définitive.
 * - de définir un offset de différence de tension pour la déconnexion de la
 * batterie.
 * - de définir un temps entre chaque enregistrement de log sur SD.
 * - de définir un courant de décharge maximal.
 * - de définir un courant de charge maximal.
 * - de connaitre la batterie avec la tension maximale, la tension minimale et
 * la tension moyenne.
 * - de connaitre l'état de la batterie, l'état de charge et de commuter la
 * batterie.
 * - de vérifier l'offset de différence de tension.
 * - etc.
 *
 * Pour plus d'informations, consultez le fichier README.md
 * @version 1.0
 * @date 2024-12-12

 */

#include "INA_NRJ_lib.h"
// #define ENABLE_WEBSERVER // Ajouter cette ligne pour activer le serveur web

const int I2C_Speed = 400; // I2C speed in KHz

// value for battery check
const int batt_check_period = 1000;  // Battery check period in ms
const float set_min_voltage = 24000; // Battery undervoltage threshold in mV
const float set_max_voltage = 30000; // Battery overvoltage threshold in mV
const int set_max_current = 1000;    // Battery overcurrent threshold in mA
const int set_max_charge_current =
    1000; // Battery charge current threshold in mA
const int set_max_discharge_current =
    1000;                          // Battery discharge current threshold in mA
const int reconnect_delay = 10000; // delay to reconnect the battery in ms
const int nb_switch_on =
    5; // number of switch on before infinite disconnect of the battery

// value for INA devices
const int max_INA_current = 50;       // Max current in A for INA devices
const int INA_micro_ohm_shunt = 2000; // Max micro ohm for INA devices

const int log_time =
    10; // Temps entre chaque enregistrement de log sur SD en secondes

#include "Batt_Parallelator_lib.h"
#include "SD_Logger.h"
#include <DebugLogger.h>
#include "pin_mapppings.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "WiFiHandler.h"

// const char *ssid = "kxkm5";
// const char *password = "";
WiFiHandler
    wifiHandler("kxkm24gocab"); // Créer une instance de la classe WiFiHandler
// wifiHandler(ssid, password);     // Créer une instance de la classe
// WiFiHandler

#ifdef ENABLE_WEBSERVER // necessaire pour le serveur web
#include "WebServerHandler.h"


WebServerHandler
    webServerHandler; // Créer une instance de la classe WebServerHandler

#endif

#include "InfluxDBHandler.h"
#include "TimeAndInfluxTask.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// Configuration InfluxDB
const char *influxDBServerUrl = "https://us-east-1-1.aws.cloud2.influxdata.com";
const char *influxDBOrg = "650b91508ddb9cf3";
const char *influxDBBucket = "BATT_PARALLELATOR";
const char *influxDBToken =
    "QTIEjAI8p9zsaxTnBYExPLyJFJNNGrx-Dz9DU0cTa6tInggXDHX77APTGsoZ7-"
    "rXgK8JqRZw6ptSdDJaoZtL8A==";
// QTIEjAI8p9zsaxTnBYExPLyJFJNNGrx-Dz9DU0cTa6tInggXDHX77APTGsoZ7-rXgK8JqRZw6ptSdDJaoZtL8A==
const bool influxDBInsecure =
    true; // Ajouter cette ligne pour définir la connexion non sécurisée

InfluxDBHandler influxDBHandler(influxDBServerUrl, influxDBOrg, influxDBBucket,
                                influxDBToken, influxDBInsecure);
/*
  NONE = 0,
    ERROR = 1 << 0,
    WARNING = 1 << 1,
    INFO = 1 << 2,
    BATTERY = 1 << 3,
    DEBUG = 1 << 4,
    I2C = 1 << 5,
    INFLUXDB = 1 << 6,
    TIME = 1 << 7
    */

DebugLogger debugLogger; // Créer une instance de la classe DebugLogger
INAHandler inaHandler;   // Créer une instance de la classe INAHandler
TCAHandler tcaHandler;   // Créer une instance de la classe TCAHandler
BATTParallelator
    BattParallelator; // Créer une instance de la classe BattParallelator
SDLogger sdLogger;    // Créer une instance de la classe SDLogger
BatteryManager batteryManager; // Créer une instance de la classe BatteryManager

String logFilename =
    "/datalog"; // Définir une variable globale pour le nom du fichier de log

const float voltage_offset = 0.5; // Offset de différence de tension en volts

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

bool isTimeSynced = false;

/**
 * @brief Scanner les appareils I2C connectés.
 */
void I2C_scanner() { // Trouver tous les appareils I2C
  byte count = 0;
  for (byte i = 8; i < 120; i++) {
    Wire.beginTransmission(i);
    delay(50);
    if (Wire.endTransmission() == 0) {
      debugLogger.printDebug(DebugLogger::I2C, "Adresse trouvée : ");
      debugLogger.printDebug(DebugLogger::I2C, String(i, DEC));
      debugLogger.printDebug(DebugLogger::I2C, " (0x");
      debugLogger.printDebug(DebugLogger::I2C, String(i, HEX));
      debugLogger.printDebug(DebugLogger::I2C, ")");
      count++;
      delay(1); // peut-être inutile ?
    }           // fin de bonne réponse
  }             // fin de la boucle for
  debugLogger.printDebug(DebugLogger::I2C, "Terminé.");
  debugLogger.printDebug(DebugLogger::I2C, "Trouvé ");
  debugLogger.printDebug(DebugLogger::I2C, String(count, DEC));
  debugLogger.printlnDebug(DebugLogger::I2C, " appareil(s).");
} // fin de I2C_scanner

/**
 * @brief Tâche pour enregistrer les données sur la carte SD.
 * @param pvParameters Paramètres de la tâche.
 */
void logDataTask(void *pvParameters); // enregistrement des données

/**
 * @brief Tâche pour vérifier la tension des batteries.
 * @param pvParameters Paramètres de la tâche.
 */
void checkBatteryVoltagesTask(void *pvParameters); // vérification des tensions

/**
 * @brief Tâche pour envoyer les données stockées sur la carte SD à InfluxDB.
 * @param pvParameters Paramètres de la tâche.
 */
void sendStoredDataTask(void *pvParameters) {
  while (true) {
    influxDBHandler.sendStoredData();
    vTaskDelay(5000 /
               portTICK_PERIOD_MS); // Attendre 1 seconde avant de réessayer
  }
  // vTaskDelete(NULL); // Supprimer la tâche après exécution
}

#ifdef ENABLE_WEBSERVER
/**
 * @brief Tâche pour gérer le serveur web.
 * @param pvParameters Paramètres de la tâche.
 */
void webServerTask(void *pvParameters) {
  while (true) {
    webServerHandler.handleClient();
    vTaskDelay(
        250 / portTICK_PERIOD_MS); // Attendre 10 ms avant de vérifier à nouveau
  }
}
#endif

/**
 * @brief Configuration initiale du programme.
 */
void setup() {
  Wire.begin(SDA_pin, SCL_pin); // sda= GPIO_32 /scl= GPIO_33
  // Wire.setClock(I2C_Speed * 1000); // définir I2C

  const bool debugLevels[DebugLogger::NUM_LEVELS] = {
    true, // NONE
    true, // ERROR
    true, // WARNING
    true, // INFO
    false, // BATTERY
    true, // DEBUG
    true, // I2C
    true, // INFLUXDB
    true, // TIME
    true, // WIFI
    true, // SD
    true, // SPIFF
    true  // WEB
  };

  debugLogger.begin(debugLevels);

  tcaHandler.begin();
  debugLogger.printlnDebug(DebugLogger::I2C, "Configuration TCA terminée");
  inaHandler.set_max_voltage(
      set_max_voltage); // définir la tension maximale en mV
  inaHandler.set_min_voltage(
      set_min_voltage); // définir la tension minimale en mV
  inaHandler.set_max_current(
      set_max_current); // définir le courant maximal en mA
  inaHandler.set_max_charge_current(
      set_max_charge_current); // définir le courant de charge maximal en mA
  inaHandler.begin(max_INA_current, INA_micro_ohm_shunt); // Initialiser les INA
  debugLogger.printlnDebug(DebugLogger::I2C, "Configuration INA terminée");

  CSVConfig csvConfig = {
      ','}; // Définir la configuration CSV avec un séparateur de virgule

  sdLogger.begin(logFilename.c_str(),
                 csvConfig); // Initialiser le logger SD avec le nom du fichier
                             // et la configuration CSV

  sdLogger.setLogTime(
      log_time); // Définir le temps entre chaque enregistrement en secondes
  debugLogger.printlnDebug(DebugLogger::I2C,
                           "Configuration du logger SD terminée");

  BattParallelator.set_max_voltage(
      set_max_voltage); // définir la tension maximale en mV
  BattParallelator.set_min_voltage(
      set_min_voltage); // définir la tension minimale en mV
  BattParallelator.set_max_current(
      set_max_current); // définir le courant maximal en mA
  BattParallelator.set_max_charge_current(
      set_max_charge_current); // définir le courant de charge maximal en mA
  BattParallelator.set_max_discharge_current(
      set_max_discharge_current); // définir le courant de décharge maximal en
                                  // mA
  BattParallelator.set_reconnect_delay(
      reconnect_delay); // définir le délai de reconnexion en ms
  BattParallelator.set_nb_switch_on(
      nb_switch_on); // définir le nombre de commutations avant de déconnecter
                     // la batterie
  BattParallelator.set_max_diff_voltage(
      voltage_offset); // Définir l'offset de différence de tension
  debugLogger.printlnDebug(
      DebugLogger::I2C, "Configuration de la gestion des batteries terminée");

  int Nb_TCA = tcaHandler.getNbTCA(); // récupérer le nombre de TCA
  int Nb_INA = inaHandler.getNbINA(); // récupérer le nombre de INA
  debugLogger.printlnDebug(DebugLogger::I2C, "");
  debugLogger.printDebug(DebugLogger::I2C, "trouvé : ");
  debugLogger.printDebug(DebugLogger::I2C, String(Nb_TCA));
  debugLogger.printDebug(DebugLogger::I2C, " INA et ");
  debugLogger.printDebug(DebugLogger::I2C, String(Nb_INA));
  debugLogger.printlnDebug(DebugLogger::I2C, " TCA");

  if (Nb_TCA !=
      Nb_INA / 4) { // Vérifier si le nombre de TCA et de INA est correct
    debugLogger.printlnDebug(
        DebugLogger::I2C,
        "Erreur : Le nombre de TCA et de INA n'est pas correct");
    if (Nb_INA % 4 != 0) {
      debugLogger.printlnDebug(DebugLogger::I2C, "Erreur : INA manquant");
    } else {
      debugLogger.printlnDebug(DebugLogger::I2C, "Erreur : TCA manquant");
    }
  } else {
    debugLogger.printlnDebug(DebugLogger::I2C,
                             "Le nombre de TCA et de INA est correct");
  }

  int detected_batteries = BattParallelator.detect_batteries();
  debugLogger.printDebug(DebugLogger::BATTERY,
                         "Nombre de batteries détectées: ");
  debugLogger.printlnDebug(DebugLogger::BATTERY, String(detected_batteries));

  // Démarrer la tâche de consommation en ampère-heure pour chaque batterie
  for (int i = 0; i < Nb_INA; i++) {
    batteryManager.startAmpereHourConsumptionTask(i, 1.0,
                                                  600); // 600 mesures par heure
  }

  // Créer une tâche pour lire les données des INA
  /*
    xTaskCreatePinnedToCore(readINADataTask, "ReadINADataTask", 4096, NULL, 1,
                            NULL, 0);
  */

  // Créer une tâche pour enregistrer les données sur la carte SD
  xTaskCreatePinnedToCore(logDataTask, "LogDataTask", 8192, NULL, 0, NULL, 1);

  // Créer une tâche pour vérifier la tension des batteries
  xTaskCreatePinnedToCore(checkBatteryVoltagesTask, "CheckBatteryVoltagesTask",
                          4096, NULL, 0, NULL, 1);

  // Créer une tâche pour envoyer les données stockées sur la carte SD à
  // InfluxDB
  xTaskCreatePinnedToCore(sendStoredDataTask, "SendStoredDataTask", 65536, NULL,
                          1, NULL, 0); // 32768

  // Créer une tâche pour gérer le temps et InfluxDB
  xTaskCreatePinnedToCore(timeAndInfluxTask, "TimeAndInfluxTask", 8192, NULL, 1,
                          NULL, 0);

  wifiHandler.begin(); // Démarrer la connexion WiFi

#ifdef ENABLE_WEBSERVER
/*
  if (!SPIFFS.begin(true)) {
    debugLogger.printlnDebug(DebugLogger::SPIFF,
                             "An error has occurred while mounting SPIFFS");
    return;
  }
  debugLogger.printlnDebug(DebugLogger::SPIFF, "SPIFFS mounted successfully");
  */
  webServerHandler.begin(); // Démarrer le serveur web

  // Créer une tâche pour gérer le serveur web

  xTaskCreatePinnedToCore(webServerTask, "WebServerTask", 12288, NULL, 1, NULL,
                          1);

#endif

  if (!SD.begin()) {
    debugLogger.printlnDebug(DebugLogger::SD, "Card Mount Failed");
    return;
  }
  debugLogger.printlnDebug(DebugLogger::SD, "SD card initialized");

  /*
  // Afficher les statistiques des tâches
  printTaskStats();

  // Afficher les statistiques d'exécution des tâches
  printRunTimeStats();
  */
}

// Fonction pour obtenir l'heure actuelle
String getCurrentTime() {
  if (isTimeSynced) {
    timeClient.update();
    debugLogger.printDebug(DebugLogger::TIME, "Time synchronized with NTP");
    return timeClient.getFormattedTime();
  } else {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char buffer[20];
      strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
      debugLogger.printDebug(DebugLogger::TIME, "RTC time available");
      return String(buffer);
    } else {
      debugLogger.printDebug(DebugLogger::TIME,
                             "RTC time not available, returning default time");
      return "1970-01-01 00:00:00"; // Valeur par défaut si l'heure n'est pas
                                    // disponible
    }
  }
}

/**
 * @brief Tâche pour enregistrer les données sur la carte SD et influxDB.
 * @param pvParameters Paramètres de la tâche.
 */
void logDataTask(void *pvParameters) {
  while (true) {
    if (sdLogger.shouldLog()) { // Vérifier si on doit enregistrer les données
      String currentTime = getCurrentTime(); // Obtenir l'heure actuelle
      debugLogger.printDebug(DebugLogger::TIME, "Current Time: ");
      debugLogger.printlnDebug(DebugLogger::TIME, currentTime);

      int Nb_Batt = inaHandler.getNbINA(); // récupérer le nombre de INA
      for (int i = 0; i < Nb_Batt; i++) {
        if (inaHandler.read_volt(i) >
            1) { // Enregistrer uniquement les batteries connectées
          sdLogger.logData(currentTime.c_str(), i, inaHandler.read_volt(i),
                           inaHandler.read_current(i),
                           BattParallelator.check_battery_status(i),
                           batteryManager.getAmpereHourConsumption(
                               i)); // Enregistrer les données sur la carte SD

          // Envoyer les données à InfluxDB
          influxDBHandler.writeBatteryData(
              "battery_data", i, inaHandler.read_volt(i),
              inaHandler.read_current(i),
              batteryManager.getAmpereHourConsumption(i));
        }
      }
    }
    vTaskDelay(
        log_time * 1000 /
        portTICK_PERIOD_MS); // Attendre log_time avant de vérifier à nouveau
  }
  // vTaskDelete(NULL); // Supprimer la tâche après exécution
}

/**
 * @brief Tâche pour vérifier la tension des batteries.
 * @param pvParameters Paramètres de la tâche.
 */
void checkBatteryVoltagesTask(void *pvParameters) {
  const float voltageOffset = 2.5; // Définir l'offset de tension en volts

  while (true) {
    float maxVoltage = batteryManager.getMaxVoltage();
    float minVoltage = batteryManager.getMinVoltage();
    float averageVoltage = batteryManager.getAverageVoltage();

    if (debugLogger.isCategoryEnabled(debugLogger.BATTERY)) {
      debugLogger.printDebug(DebugLogger::BATTERY, "Max Voltage: ");
      debugLogger.printlnDebug(DebugLogger::BATTERY, String(maxVoltage));
      debugLogger.printDebug(DebugLogger::BATTERY, "Min Voltage: ");
      debugLogger.printlnDebug(DebugLogger::BATTERY, String(minVoltage));
      debugLogger.printDebug(DebugLogger::BATTERY, "Average Voltage: ");
      debugLogger.printlnDebug(DebugLogger::BATTERY, String(averageVoltage));
    }

    int Nb_Batt = inaHandler.getNbINA();
    for (int i = 0; i < Nb_Batt; i++) {
      float voltage = inaHandler.read_volt(i);
      if (voltage > 1) {
        if (debugLogger.isCategoryEnabled(debugLogger.BATTERY)) {
          debugLogger.printDebug(DebugLogger::BATTERY, "Battery ");
          debugLogger.printDebug(DebugLogger::BATTERY, String(i));
          debugLogger.printDebug(DebugLogger::BATTERY, " Voltage :");
          debugLogger.printDebug(DebugLogger::BATTERY, String(voltage));
          debugLogger.printDebug(DebugLogger::BATTERY, "  Current :");
          debugLogger.printlnDebug(DebugLogger::BATTERY,
                                   String(inaHandler.read_current(i)));
        }
      }
      if (!BattParallelator.check_voltage_offset(i, voltageOffset)) {
        BattParallelator.check_battery_connected_status(
            i); // Utiliser les valeurs pour vérifier l'état de la batterie
      }
    }

    vTaskDelay(batt_check_period /
               portTICK_PERIOD_MS); // Attendre batt_check_period avant de
                                    // vérifier à nouveau
  }
}

/**
 * @brief Boucle principale du programme.
 */
void loop() {
  // Laisser les tâches FreeRTOS gérer les opérations
// vTaskDelay(1000 / portTICK_PERIOD_MS); // Ajouter un léger délai pour
  // éviter une boucle serrée
}