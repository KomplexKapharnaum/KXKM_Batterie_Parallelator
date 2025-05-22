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
#include <cmath>
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
    1; // Temps entre chaque enregistrement de log sur SD en secondes

// Base du nom de fichier pour les logs (sans extension)

// const char *logFileBase = "datalog_gocab";
//const char *logFileBase = "datalog_tender";
 const char *logFileBase = "datalog_k-led1";
// const char *logFileBase = "datalog_k-led2";
// const char *logFileBase = "datalog_christiana";
// const char *logFileBase = "datalog_clayton1";
// const char *logFileBase = "datalog_k-led1";

#include "BatteryParallelator.h"
#include "SD_Logger.h"
#include "WiFiHandler.h"
#include "pin_mapppings.h"
#include <Arduino.h>
#include <DebugLogger.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// const char *ssid = "kxkm5";
// const char *password = "";
WiFiHandler
    wifiHandler("kxkm24gocab"); // Créer une instance de la classe WiFiHandler
// wifiHandler(ssid, password);     // Créer une instance de la classe
// WiFiHandler

// Macros pour activer/désactiver les fonctionnalités
// #define ENABLE_WEBSERVER
// #define ENABLE_TIME_AND_INFLUXDB
#define ENABLE_SD_LOGGING

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
      debugLogger.print(DebugLogger::I2C, "Adresse trouvée : ");
      debugLogger.print(DebugLogger::I2C, String(i, DEC));
      debugLogger.print(DebugLogger::I2C, " (0x");
      debugLogger.print(DebugLogger::I2C, String(i, HEX));
      debugLogger.print(DebugLogger::I2C, ")");
      count++;
      delay(1); // peut-être inutile ?
    } // fin de bonne réponse
  } // fin de la boucle for
  debugLogger.print(DebugLogger::I2C, "Terminé.");
  debugLogger.print(DebugLogger::I2C, "Trouvé ");
  debugLogger.print(DebugLogger::I2C, String(count, DEC));
  debugLogger.println(DebugLogger::I2C, " appareil(s).");
} // fin de I2C_scanner

#ifdef ENABLE_SD_LOGGING
/**
 * @brief Tâche pour enregistrer les données sur la carte SD.
 * @param pvParameters Paramètres de la tâche.
 */
void logDataTask(void *pvParameters); // enregistrement des données
#endif

/**
 * @brief Tâche pour vérifier la tension des batteries.
 * @param pvParameters Paramètres de la tâche.
 */
void checkBatteryTask(void *pvParameters); // vérification des tensions

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

// Définir le nombre de niveaux de débogage explicitement
const int NUM_DEBUG_LEVELS =
    13; // Correspond au nombre de niveaux dans DebugLogger::DebugLevel

/**
 * @brief Configuration initiale du programme.
 */
void setup() {
  Wire.begin(SDA_pin, SCL_pin); // sda= GPIO_32 /scl= GPIO_33
  // Wire.setClock(I2C_Speed * 1000); // définir I2C

  const DebugLogger::DebugLevelInfo debugLevels[NUM_DEBUG_LEVELS] = {
      {"NONE", true}, {"ERROR", true},    {"WARNING", true},
      {"INFO", true}, {"DEBUG", true},    {"BATTERY", false},
      {"I2C", true},  {"INFLUXDB", true}, {"TIME", true},
      {"WIFI", true}, {"SD", true},       {"SPIFF", true},
      {"WEB", true}};

  debugLogger.begin(debugLevels, NUM_DEBUG_LEVELS);

  tcaHandler.begin();
  debugLogger.println(DebugLogger::I2C, "Configuration TCA terminée");
  inaHandler.set_max_voltage(
      set_max_voltage); // définir la tension maximale en mV
  inaHandler.set_min_voltage(
      set_min_voltage); // définir la tension minimale en mV
  inaHandler.set_max_current(
      set_max_current); // définir le courant maximal en mA
  inaHandler.set_max_charge_current(
      set_max_charge_current); // définir le courant de charge maximal en mA
  inaHandler.begin(max_INA_current, INA_micro_ohm_shunt); // Initialiser les INA
  debugLogger.println(DebugLogger::I2C, "Configuration INA terminée");

  CSVConfig csvConfig = {
      ';'}; // Définir la configuration de séparateur CSV

#ifdef ENABLE_SD_LOGGING
  // Appeler begin() avec la base du nom de fichier pour permettre
  // l'incrémentation
  sdLogger.begin(logFileBase, csvConfig);
  sdLogger.setLogTime(log_time);
  debugLogger.println(DebugLogger::I2C, "Configuration du logger SD terminée");
#endif

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
  debugLogger.println(DebugLogger::I2C,
                      "Configuration de la gestion des batteries terminée");

  int Nb_TCA = tcaHandler.getNbTCA(); // récupérer le nombre de TCA
  int Nb_INA = inaHandler.getNbINA(); // récupérer le nombre de INA
  debugLogger.println(DebugLogger::I2C, "");
  debugLogger.print(DebugLogger::I2C, "trouvé : ");
  debugLogger.print(DebugLogger::I2C, String(Nb_TCA));
  debugLogger.print(DebugLogger::I2C, " INA et ");
  debugLogger.print(DebugLogger::I2C, String(Nb_INA));
  debugLogger.println(DebugLogger::I2C, " TCA");

  if (Nb_TCA !=
      Nb_INA / 4) { // Vérifier si le nombre de TCA et de INA est correct
    debugLogger.println(
        DebugLogger::I2C,
        "Erreur : Le nombre de TCA et de INA n'est pas correct");
    if (Nb_INA % 4 != 0) {
      debugLogger.println(DebugLogger::I2C, "Erreur : INA manquant");
    } else {
      debugLogger.println(DebugLogger::I2C, "Erreur : TCA manquant");
    }
  } else {
    debugLogger.println(DebugLogger::I2C,
                        "Le nombre de TCA et de INA est correct");
  }

  int detected_batteries = BattParallelator.detect_batteries();
  debugLogger.print(DebugLogger::BATTERY, "Nombre de batteries détectées: ");
  debugLogger.println(DebugLogger::BATTERY, String(detected_batteries));

  // Démarrer la tâche de consommation en ampère-heure pour chaque batterie
  for (int i = 0; i < Nb_INA; i++) {
    batteryManager.startAmpereHourConsumptionTask(i, 1.0,
                                                  600); // 600 mesures par heure
  }

  // Vérification initiale des batteries pour la première connexion
  int Nb_Batt = inaHandler.getNbINA();
  for (int i = 0; i < Nb_Batt; i++) {
    BattParallelator.check_battery_connected_status(i);
  }

  // Créer une tâche pour lire les données des INA
  /*
    xTaskCreatePinnedToCore(readINADataTask, "ReadINADataTask", 4096, NULL, 1,
                            NULL, 0);
  */
#ifdef ENABLE_SD_LOGGING
  // Créer une tâche pour enregistrer les données sur la carte SD
  xTaskCreatePinnedToCore(logDataTask, "LogDataTask", 8192, NULL, 0, NULL, 1);
#endif
sdLogger.setBatteryCount(Nb_Batt); // Définir le nombre de batteries

  // Créer une tâche pour vérifier la tension des batteries
  xTaskCreatePinnedToCore(checkBatteryTask, "checkBatteryTask", 4096, NULL, 0,
                          NULL, 1);

#ifdef ENABLE_TIME_AND_INFLUXDB
  // Créer une tâche pour envoyer les données stockées sur la carte SD à
  // InfluxDB
  xTaskCreatePinnedToCore(sendStoredDataTask, "SendStoredDataTask", 65536, NULL,
                          1, NULL, 0); // 32768

  // Créer une tâche pour gérer le temps et InfluxDB
  xTaskCreatePinnedToCore(timeAndInfluxTask, "TimeAndInfluxTask", 8192, NULL, 1,
                          NULL, 0);

  wifiHandler.begin(); // Démarrer la connexion WiFi
#endif

#ifdef ENABLE_WEBSERVER
  /*
    if (!SPIFFS.begin(true)) {
      debugLogger.println(DebugLogger::SPIFF,
                               "An error has occurred while mounting SPIFFS");
      return;
    }
    debugLogger.println(DebugLogger::SPIFF, "SPIFFS mounted successfully");
    */
  webServerHandler.begin(); // Démarrer le serveur web

  // Créer une tâche pour gérer le serveur web

  xTaskCreatePinnedToCore(webServerTask, "WebServerTask", 12288, NULL, 1, NULL,
                          1);

#endif

#ifdef ENABLE_SD_LOGGING
  if (!SD.begin()) {
    debugLogger.println(DebugLogger::SD, "Card Mount Failed");
    return;
  }
  debugLogger.println(DebugLogger::SD, "SD card initialized");
#endif

#ifdef ENABLE_TIME_AND_INFLUXDB
  xTaskCreatePinnedToCore(timeAndInfluxTask, "TimeAndInfluxTask", 8192, NULL, 1,
                          NULL, 0);
#endif
}

#ifdef ENABLE_TIME_AND_INFLUXDB
// Fonction pour obtenir l'heure actuelle
String getCurrentTime() {
  if (isTimeSynced) {
    timeClient.update();
    debugLogger.print(DebugLogger::TIME, "Time synchronized with NTP");
    return timeClient.getFormattedTime();
  } else {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char buffer[20];
      strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
      debugLogger.print(DebugLogger::TIME, "RTC time available");
      return String(buffer);
    } else {
      debugLogger.print(DebugLogger::TIME,
                        "RTC time not available, returning default time");
      return "1970-01-01 00:00:00"; // Valeur par défaut si l'heure n'est pas
                                    // disponible
    }
  }
}
#endif
// si ENABLE_TIME_AND_INFLUXDB n'est pas défini, la fonction getCurrentTime est
// remplacé par l'heure de compilation
#ifndef ENABLE_TIME_AND_INFLUXDB
#include <SD.h> // Ajout pour accès SD

// Fonction utilitaire pour extraire la date/heure de la dernière ligne du dernier fichier log
time_t getLastLogTimeFromSD(const char* logFileBase) {
  File root = SD.open("/");
  String lastFileName = "";
  int maxNum = 0;
  // Recherche du dernier fichier log (format: base_xxx.csv)
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    String name = String(entry.name());
    if (name.startsWith("/")) name = name.substring(1);
    if (name.startsWith(logFileBase) && name.endsWith(".csv")) {
      int underscore = name.lastIndexOf('_');
      int dot = name.lastIndexOf('.');
      if (underscore > 0 && dot > underscore) {
        int num = name.substring(underscore + 1, dot).toInt();
        if (num > maxNum) {
          maxNum = num;
          lastFileName = "/" + name;
        }
      }
    }
    entry.close();
  }
  root.close();
  if (lastFileName == "") return 0;

  File lastFile = SD.open(lastFileName.c_str(), FILE_READ);
  if (!lastFile) return 0;
  String lastLine;
  while (lastFile.available()) {
    lastLine = lastFile.readStringUntil('\n');
  }
  lastFile.close();
  int sep = lastLine.indexOf(';');
  String dateStr = (sep > 0) ? lastLine.substring(0, sep) : lastLine;
  struct tm timeinfo;
  memset(&timeinfo, 0, sizeof(struct tm));
  // On tente d'abord le format log "YYYY-MM-DD HH:MM:SS"
  if (sscanf(dateStr.c_str(), "%d-%d-%d %d:%d:%d",
             &timeinfo.tm_year, &timeinfo.tm_mon, &timeinfo.tm_mday,
             &timeinfo.tm_hour, &timeinfo.tm_min, &timeinfo.tm_sec) == 6) {
    timeinfo.tm_year -= 1900;
    timeinfo.tm_mon -= 1;
    return mktime(&timeinfo);
  }
  // Sinon, on tente le format compilation "MMM DD YYYY HH:MM:SS"
  char month[4];
  int day, year, hour, minute, second;
  if (sscanf(dateStr.c_str(), "%3s %d %d %d:%d:%d", month, &day, &year, &hour, &minute, &second) == 6) {
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int monthNum = 0;
    for (int i = 0; i < 12; i++) {
      if (strcmp(month, months[i]) == 0) {
        monthNum = i;
        break;
      }
    }
    memset(&timeinfo, 0, sizeof(struct tm));
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = monthNum;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    return mktime(&timeinfo);
  }
  return 0;
}

// récupérer l'heure depuis l'heure de compilation ou du dernier log SD
String getCurrentTime() {
  static time_t compilationTime = 0;
  static bool initialized = false;

  if (!initialized) {
    // Initialiser avec la date du dernier log si dispo, sinon compilation
    time_t lastLogTime = 0;
    if (SD.begin()) {
      lastLogTime = getLastLogTimeFromSD(logFileBase);
    }
    if (lastLogTime > 0) {
      compilationTime = lastLogTime;
    } else {
      // Initialiser avec l'heure de compilation une seule fois
      const char *compileDate = __DATE__; // Format: "MMM DD YYYY"
      const char *compileTime = __TIME__; // Format: "HH:MM:SS"

      char month[4];
      int day, year, hour, minute, second;

      sscanf(compileDate, "%s %d %d", month, &day, &year);
      sscanf(compileTime, "%d:%d:%d", &hour, &minute, &second);

      // Convertir le mois en nombre
      const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
      int monthNum = 0;
      for (int i = 0; i < 12; i++) {
        if (strcmp(month, months[i]) == 0) {
          monthNum = i;
          break;
        }
      }

      struct tm timeinfo;
      memset(&timeinfo, 0, sizeof(struct tm));
      timeinfo.tm_year = year - 1900;
      timeinfo.tm_mon = monthNum;
      timeinfo.tm_mday = day;
      timeinfo.tm_hour = hour;
      timeinfo.tm_min = minute;
      timeinfo.tm_sec = second;

      compilationTime = mktime(&timeinfo);
    }
    initialized = true;
  }

  // Calculer l'heure actuelle en ajoutant le temps écoulé depuis le démarrage
  time_t now = compilationTime + (millis() / 1000);
  struct tm *timeinfo = localtime(&now);

  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

  return String(buffer);
}
#endif

#ifdef ENABLE_SD_LOGGING
/**
 * @brief Tâche pour enregistrer les données sur la carte SD et influxDB.
 * @param pvParameters Paramètres de la tâche.
 */
void logDataTask(void *pvParameters) {
  while (true) {
    if (sdLogger.shouldLog()) {
      String currentTime = getCurrentTime();
      debugLogger.print(DebugLogger::TIME, "Current Time is : ");
      debugLogger.println(DebugLogger::TIME, currentTime);

      // Calculer d'abord les valeurs totales
      float totalConsumption = batteryManager.getTotalConsumption();
      float totalCharge = batteryManager.getTotalCharge();
      float totalCurrent = batteryManager.getTotalCurrent();

      debugLogger.print(DebugLogger::SD, "Totaux - Décharge: ");
      debugLogger.print(DebugLogger::SD, String(totalConsumption));
      debugLogger.print(DebugLogger::SD, " Ah, Charge: ");
      debugLogger.print(DebugLogger::SD, String(totalCharge));
      debugLogger.print(DebugLogger::SD, " Ah, Courant: ");
      debugLogger.print(DebugLogger::SD, String(totalCurrent));
      debugLogger.println(DebugLogger::SD, " A");
      debugLogger.println(
          DebugLogger::SD,
          "--------------------------------------------------------"
          "-----------------------------------------------");

      int Nb_Batt = inaHandler.getNbINA();
      for (int i = 0; i < Nb_Batt; i++) {
        if (inaHandler.read_volt(i) > 1) { // Batteries connectées uniquement
          float voltage = inaHandler.read_volt(i);
          float current = inaHandler.read_current(i);
          bool switchState = BattParallelator.check_battery_status(i);
          float ampereHourConsumption =
              batteryManager.getAmpereHourConsumption(i);
          float ampereHourCharge = batteryManager.getAmpereHourCharge(i);

          // Enregistrer toutes les données, y compris les totaux
          sdLogger.logData(currentTime.c_str(), i, voltage, current,
                           switchState, ampereHourConsumption, ampereHourCharge,
                           totalConsumption, totalCharge, totalCurrent);

#ifdef ENABLE_TIME_AND_INFLUXDB
          // Envoyer les données à InfluxDB
          influxDBHandler.writeBatteryData(
              "battery_data", i, voltage, current, ampereHourConsumption,
              ampereHourCharge, totalConsumption, totalCharge, totalCurrent);
#endif
        }
      }
      sdLogger.flushLine(); // après toutes les batteries
    }
    vTaskDelay(log_time * 1000 / portTICK_PERIOD_MS);
  }
}
#endif

/**
 * @brief Affiche un tableau de debug avec le numéro de batterie, la tension, le
 * courant, les états des E/S TCA et les messages d'analyse.
 */
void debugBatteryTable() {
  int Nb_Batt = inaHandler.getNbINA();
  float averageVoltage = batteryManager.getAverageVoltage();
  float total_current = 0.0;

  debugLogger.println(DebugLogger::BATTERY,
                      "--------------------------------------------------------"
                      "----------------------------------"
                      "-----------------------------------------------");
  debugLogger.println(
      DebugLogger::BATTERY,
      "| Batterie | Tension (V) | Courant (A) | Ah      | TCA | OUT | LED "
      "Rouge | LED Verte | Etat Switch | Analyse             |");
  debugLogger.println(DebugLogger::BATTERY,
                      "--------------------------------------------------------"
                      "----------------------------------"
                      "-----------------------------------------------");
  for (int i = 0; i < Nb_Batt; i++) { // Boucle à travers chaque batterie
    float voltage = inaHandler.read_volt(i);
    float current = inaHandler.read_current(i);
    float ah = batteryManager.getAmpereHourConsumption(i);
    int tcaNum = BattParallelator.TCA_num(i);
    int outNum = (inaHandler.getDeviceAddress(i) - 64) % 4;
    total_current += current;
    // Lire les états des E/S TCA
    int switchState = tcaHandler.read(tcaNum, outNum);       // set pin N° 0
    int ledRedState = tcaHandler.read(tcaNum, outNum + 8);   // set pin N° 8
    int ledGreenState = tcaHandler.read(tcaNum, outNum + 9); // set pin N° 9

    // Analyse des messages
    String analysis = "";
    if (abs(voltage - averageVoltage) > voltage_offset) {
      analysis += "out of range";
    }
    if (voltage < set_min_voltage / 1000) {
      analysis += "too low";
    }
    if (voltage > set_max_voltage / 1000) {
      analysis += "too high";
    }
    /*
    if (BattParallelator.compare_voltage(voltage, set_max_voltage / 1000,
                                         voltage_offset)) {
      analysis += "different; ";
    }
    */
    debugLogger.print(DebugLogger::BATTERY, "| ");
    debugLogger.print(DebugLogger::BATTERY, String(i + 1));
    debugLogger.print(DebugLogger::BATTERY, "        | ");
    debugLogger.print(DebugLogger::BATTERY, String(voltage, 2));
    debugLogger.print(DebugLogger::BATTERY, "       | ");
    debugLogger.print(DebugLogger::BATTERY, String(current, 2));
    debugLogger.print(DebugLogger::BATTERY, "       | ");
    debugLogger.print(DebugLogger::BATTERY, String(ah, 3));
    debugLogger.print(DebugLogger::BATTERY, "  | ");
    debugLogger.print(DebugLogger::BATTERY, String(tcaNum));
    debugLogger.print(DebugLogger::BATTERY, "   | ");
    debugLogger.print(DebugLogger::BATTERY, String(outNum));
    debugLogger.print(DebugLogger::BATTERY, "   | ");
    debugLogger.print(DebugLogger::BATTERY, String(ledRedState));
    debugLogger.print(DebugLogger::BATTERY, "         | ");
    debugLogger.print(DebugLogger::BATTERY, String(ledGreenState));
    debugLogger.print(DebugLogger::BATTERY, "         | ");
    debugLogger.print(DebugLogger::BATTERY, String(switchState));
    debugLogger.print(DebugLogger::BATTERY, "          | ");
    debugLogger.print(DebugLogger::BATTERY, analysis);
    debugLogger.println(DebugLogger::BATTERY, " |");
  }
  debugLogger.println(DebugLogger::BATTERY,
                      "Total Current: " + String(total_current, 2) + " A");
  debugLogger.println(DebugLogger::BATTERY,
                      "--------------------------------------------------------"
                      "----------------------------------"
                      "-----------------------------------------------");
}

/**
 * @brief Tâche pour vérifier la tension des batteries et tenter de les
 * connecter si elles sont déconnectées.
 * @param pvParameters Paramètres de la tâche.
 */
void checkBatteryTask(void *pvParameters) {
  const float voltageOffset = 2.5; // Définir l'offset de tension en volts
  while (true) {
    int Nb_Batt = inaHandler.getNbINA();
    debugLogger.println(DebugLogger::BATTERY,
                        "Vérification de la tension des batteries...");
    debugLogger.println(
        DebugLogger::BATTERY,
        "--------------------------------------------------------"
        "-----------------------------------------------------");

    // Remplir le tableau battery_voltages
    for (int i = 0; i < Nb_Batt; i++) {
      BattParallelator.battery_voltages[i] = inaHandler.read_volt(i);
    }

    // Calculer la tension maximale
    float max_voltage = BattParallelator.find_max_voltage(
        BattParallelator.battery_voltages, Nb_Batt);

    // TODO : faire fonctionner la gestion de la batterie
    /*
// Parcourir toutes les batteries pour vérifier leur état
for (int i = 0; i < Nb_Batt; i++) {
  // Vérifier si la batterie est connectée
  if (!BattParallelator.check_voltage_offset(i, voltageOffset)) {
    BattParallelator.check_battery_connected_status(
        i); // Utiliser les valeurs pour vérifier l'état de la batterie
  }
}
  */

    // Afficher les informations détaillées sur les batteries si le débogage est
    // activé
    debugBatteryTable();

    // Attendre avant de vérifier à nouveau
    vTaskDelay(batt_check_period / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Boucle principale du programme.
 */
void loop() {
  // Laisser les tâches FreeRTOS gérer les opérations
  vTaskDelay(1000 / portTICK_PERIOD_MS); // Ajouter un léger délai pour
  //  éviter une boucle serrée
}