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

// API tokens loaded from credentials.h (not committed to git)
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

 const char *logFileBase = "datalog_gocab";
// const char *logFileBase = "datalog_tender";
// const char *logFileBase = "datalog_k-led1";
// const char *logFileBase = "datalog_k-led2";
// const char *logFileBase = "datalog_christiana";
// const char *logFileBase = "datalog_clayton1";
// const char *logFileBase = "datalog_k-led1";

#include "BatteryParallelator.h"
#include "I2CMutex.h"
#include "MQTTHandler.h"
#include "SD_Logger.h"
#include "WiFiHandler.h"
#include "pin_mapppings.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <DebugLogger.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Global I2C mutex — serializes all Wire transactions across tasks
SemaphoreHandle_t i2cMutex = NULL;

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
// #define ENABLE_MQTT

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

// Configuration InfluxDB — credentials loaded from credentials.h
#include "credentials.h"

InfluxDBHandler influxDBHandler(influxDBServerUrl, influxDBOrg, influxDBBucket,
                                influxDBToken, influxDBInsecure);

#ifdef ENABLE_MQTT
MQTTHandler mqttHandler(mqttBrokerHost, mqttBrokerPort, mqttClientId,
                        mqttUsername, mqttPassword);
static constexpr uint32_t kMqttPublishIntervalMs = 30000;
#endif
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

enum class CloudIngestionMode {
  NORMAL,
  GAME,
};

#ifndef CLOUD_INGESTION_MODE_GAME
#define CLOUD_INGESTION_MODE_GAME 0
#endif

static constexpr uint32_t kCloudSendIntervalNormalMs = 4UL * 60UL * 60UL * 1000UL;
static constexpr uint32_t kCloudSendIntervalGameMs = 30UL * 1000UL;
static volatile CloudIngestionMode g_cloudIngestionMode =
    (CLOUD_INGESTION_MODE_GAME != 0) ? CloudIngestionMode::GAME
                                     : CloudIngestionMode::NORMAL;

static constexpr int kTaskWdtTimeoutSeconds = 15;
static constexpr uint32_t kBrownoutSafeModeThreshold = 3;
static constexpr uint32_t kBrownoutCounterClearUptimeMs = 120000;
RTC_DATA_ATTR static uint32_t g_brownoutResetCount = 0;
static bool g_safeModeActive = false;
static bool g_brownoutCounterCleared = false;

static void registerCurrentTaskToWdt(const char *taskName) {
  const esp_err_t addErr = esp_task_wdt_add(nullptr);
  if (addErr != ESP_OK) {
    debugLogger.println(DebugLogger::WARNING,
                        String("WDT add failed for ") + taskName +
                            " err=" + String(static_cast<int>(addErr)));
  }
}

/**
 * @brief Scanner les appareils I2C connectés.
 */
void I2C_scanner() { // Trouver tous les appareils I2C
  byte count = 0;
  for (byte i = 8; i < 120; i++) {
    Wire.beginTransmission(i);
    delay(1);
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
  registerCurrentTaskToWdt("SendStoredDataTask");
  while (true) {
    influxDBHandler.sendStoredData();
    esp_task_wdt_reset();
    vTaskDelay(5000 /
               portTICK_PERIOD_MS); // Attendre 1 seconde avant de réessayer
  }
  // vTaskDelete(NULL); // Supprimer la tâche après exécution
}

#ifdef ENABLE_MQTT
/**
 * @brief Tâche MQTT pour publier une télémétrie agrégée vers le broker.
 * @param pvParameters Paramètres de la tâche.
 */
void mqttTelemetryTask(void *pvParameters) {
  registerCurrentTaskToWdt("MqttTelemetryTask");
  mqttHandler.begin();
  uint32_t lastPublishMs = 0;

  while (true) {
    mqttHandler.loop();

    const uint32_t nowMs = millis();
    const bool shouldPublish =
        (lastPublishMs == 0) || ((nowMs - lastPublishMs) >= kMqttPublishIntervalMs);
    if (shouldPublish) {
      DynamicJsonDocument doc(512);
      doc["ts_ms"] = nowMs;
      doc["battery_count"] = inaHandler.getNbINA();
      doc["total_current"] = batteryManager.getTotalCurrent();
      doc["total_charge"] = batteryManager.getTotalCharge();
      doc["total_consumption"] = batteryManager.getTotalConsumption();
      doc["safe_mode"] = g_safeModeActive;

      String payload;
      serializeJson(doc, payload);
      const bool published = mqttHandler.publishJson(mqttTopicTelemetry, payload);
      if (!published) {
        debugLogger.println(DebugLogger::WARNING,
                            "MQTT publish skipped/failed for aggregated telemetry");
      }
      lastPublishMs = nowMs;
    }

    esp_task_wdt_reset();
    vTaskDelay(250 / portTICK_PERIOD_MS);
  }
}
#endif

#ifdef ENABLE_WEBSERVER
/**
 * @brief Tâche pour gérer le serveur web.
 * @param pvParameters Paramètres de la tâche.
 */
void webServerTask(void *pvParameters) {
  registerCurrentTaskToWdt("WebServerTask");
  while (true) {
    webServerHandler.handleClient();
    esp_task_wdt_reset();
    vTaskDelay(
        250 / portTICK_PERIOD_MS); // Attendre 10 ms avant de vérifier à nouveau
  }
}
#endif

// Définir le nombre de niveaux de débogage explicitement
const int NUM_DEBUG_LEVELS =
    13; // Correspond au nombre de niveaux dans DebugLogger::DebugLevel

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

#ifdef ENABLE_SD_LOGGING
/**
 * @brief Tâche pour enregistrer les données sur la carte SD et influxDB.
 * @param pvParameters Paramètres de la tâche.
 */
void logDataTask(void *pvParameters) {
  registerCurrentTaskToWdt("LogDataTask");
#ifdef ENABLE_TIME_AND_INFLUXDB
  uint32_t lastCloudAggregationMs = 0;
  bool wasCritical[16] = {false};
#endif
  while (true) {
    if (sdLogger.shouldLog()) {
      String currentTime = sdLogger.getCurrentTime(logFileBase); // Correction ici
      debugLogger.print(DebugLogger::TIME, "Current Time is : ");
      debugLogger.println(DebugLogger::TIME, currentTime);

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
      int logged = 0;
    #ifdef ENABLE_TIME_AND_INFLUXDB
        const uint32_t nowMs = millis();
        const uint32_t activeIntervalMs =
          (g_cloudIngestionMode == CloudIngestionMode::GAME)
            ? kCloudSendIntervalGameMs
            : kCloudSendIntervalNormalMs;
        const bool shouldSendAggregated =
          (lastCloudAggregationMs == 0) ||
          ((nowMs - lastCloudAggregationMs) >= activeIntervalMs);

        String aggregatedFields =
          "total_consumption=" + String(totalConsumption, 6) +
          ",total_charge=" + String(totalCharge, 6) +
          ",total_current=" + String(totalCurrent, 6) +
          ",battery_count=" + String(Nb_Batt) + "i";
    #endif

      for (int i = 0; i < Nb_Batt; i++) {
          float voltage = inaHandler.read_volt(i);
          float current = inaHandler.read_current(i);
          bool switchState = BattParallelator.check_battery_status(i);
          float ampereHourConsumption =
              batteryManager.getAmpereHourConsumption(i);
          float ampereHourCharge = batteryManager.getAmpereHourCharge(i);

          debugLogger.print(DebugLogger::SD, "logDataTask: Logging battery ");
          debugLogger.println(DebugLogger::SD, String(i));
          sdLogger.logData(currentTime.c_str(), i, voltage, current,
                           switchState, ampereHourConsumption, ampereHourCharge,
                           totalConsumption, totalCharge, totalCurrent);
          logged++;
#ifdef ENABLE_TIME_AND_INFLUXDB
          const bool criticalVoltage =
              (!std::isnan(voltage)) &&
              ((voltage * 1000.0f < set_min_voltage) ||
               (voltage * 1000.0f > set_max_voltage));
          const bool criticalCurrent =
              (!std::isnan(current)) &&
              ((std::abs(current) * 1000.0f) > set_max_current);
          const bool isCritical = criticalVoltage || criticalCurrent;

          if (i >= 0 && i < 16) {
            if (isCritical && !wasCritical[i]) {
              influxDBHandler.writeBatteryData(
                  "battery_critical_event", i, voltage, current,
                  ampereHourConsumption, ampereHourCharge, totalConsumption,
                  totalCharge, totalCurrent);
            }
            wasCritical[i] = isCritical;
          }

          if (shouldSendAggregated) {
            aggregatedFields += ",b" + String(i) + "_v=" + String(voltage, 6);
            aggregatedFields += ",b" + String(i) + "_i=" + String(current, 6);
            aggregatedFields += ",b" + String(i) + "_sw=" +
                               String(switchState ? 1 : 0) + "i";
            aggregatedFields += ",b" + String(i) + "_crit=" +
                               String(isCritical ? 1 : 0) + "i";
          }
#endif
        
      }

#ifdef ENABLE_TIME_AND_INFLUXDB
      if (shouldSendAggregated && logged > 0) {
        influxDBHandler.writeAggregatedBatteryData(
            "battery_data_aggregated",
            (g_cloudIngestionMode == CloudIngestionMode::GAME) ? "game"
                                                                : "normal",
            aggregatedFields);
        lastCloudAggregationMs = nowMs;
      }
#endif

      if (logged == 0) {
        debugLogger.println(DebugLogger::WARNING, "logDataTask: Aucune batterie loggée !");
      }
      debugLogger.println(DebugLogger::SD, "logDataTask: flushLine()");
      sdLogger.flushLine(); // après toutes les batteries
    }
    esp_task_wdt_reset();
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
    if (!std::isnan(current)) total_current += current;
    // Lire les états des E/S TCA
    int switchState = tcaHandler.read(tcaNum, outNum);           // switch pin
    int ledRedState = tcaHandler.read(tcaNum, outNum * 2 + 8);   // red LED pin
    int ledGreenState = tcaHandler.read(tcaNum, outNum * 2 + 9); // green LED pin

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
  registerCurrentTaskToWdt("CheckBatteryTask");
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
      float v = inaHandler.read_volt(i);
      BattParallelator.set_battery_voltage(i, std::isnan(v) ? 0.0f : v);
    }

    // Calculer la tension maximale
    float batterySnapshot[16] = {0.0f};
    BattParallelator.copy_battery_voltages(batterySnapshot, Nb_Batt);
    float max_voltage = BattParallelator.find_max_voltage(
        batterySnapshot, Nb_Batt);
    (void)max_voltage;

    // Battery protection: check voltage offset and connection status
    for (int i = 0; i < Nb_Batt; i++) {
      if (!BattParallelator.check_voltage_offset(i, voltageOffset)) {
        BattParallelator.check_battery_connected_status(i);
      }
    }

    // Afficher les informations détaillées sur les batteries si le débogage est
    // activé
    debugBatteryTable();

    esp_task_wdt_reset();

    // Attendre avant de vérifier à nouveau
    vTaskDelay(batt_check_period / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Configuration initiale du programme.
 */
void setup() {
  i2cMutexInit(); // Must be before any I2C or task operations
  Wire.begin(SDA_pin, SCL_pin); // sda= GPIO_32 /scl= GPIO_33
  // Wire.setClock(I2C_Speed * 1000); // définir I2C

  const DebugLogger::DebugLevelInfo debugLevels[NUM_DEBUG_LEVELS] = {
      {"NONE", true}, {"ERROR", true},    {"WARNING", true},
      {"INFO", true}, {"DEBUG", true},    {"BATTERY", false},
      {"I2C", true},  {"INFLUXDB", true}, {"TIME", true},
      {"WIFI", true}, {"SD", true},       {"SPIFF", true},
      {"WEB", true}};

  debugLogger.begin(debugLevels, NUM_DEBUG_LEVELS);

  const esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_BROWNOUT) {
    g_brownoutResetCount++;
  } else if (resetReason == ESP_RST_POWERON) {
    g_brownoutResetCount = 0;
  }

  if (g_brownoutResetCount >= kBrownoutSafeModeThreshold) {
    g_safeModeActive = true;
    debugLogger.println(DebugLogger::WARNING,
                        "Safe mode enabled after repeated brownout resets");
  }

  const esp_err_t wdtInitErr =
      esp_task_wdt_init(kTaskWdtTimeoutSeconds, true);
  if (wdtInitErr != ESP_OK) {
    debugLogger.println(DebugLogger::WARNING,
                        "Task WDT init failed err=" +
                            String(static_cast<int>(wdtInitErr)));
  } else {
    debugLogger.println(DebugLogger::INFO,
                        "Task WDT initialized timeout=" +
                            String(kTaskWdtTimeoutSeconds) + "s");
  }

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

  int Nb_Batt = inaHandler.getNbINA();
  for (int i = 0; i < Nb_Batt; i++) {
    BattParallelator.check_battery_connected_status(i);
  }

#ifdef ENABLE_SD_LOGGING
  sdLogger.setBatteryCount(Nb_Batt);
  debugLogger.print(DebugLogger::SD, "Nombre de batteries : ");
  debugLogger.println(DebugLogger::SD, String(Nb_Batt));
  CSVConfig csvConfig = {';'};
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
        "FATAL: Le nombre de TCA et de INA n'est pas correct");
    if (Nb_INA % 4 != 0) {
      debugLogger.println(DebugLogger::I2C, "FATAL: INA manquant");
    } else {
      debugLogger.println(DebugLogger::I2C, "FATAL: TCA manquant");
    }
    debugLogger.println(DebugLogger::I2C,
                        "HALT: Protection controller cannot operate with invalid HW topology");
    while (true) { delay(1000); } // Halt — do not operate with mismatched sensors
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

  for (int i = 0; i < Nb_Batt; i++) {
    BattParallelator.check_battery_connected_status(i);
  }

#ifdef ENABLE_SD_LOGGING
  // Créer une tâche pour enregistrer les données sur la carte SD
  if (!g_safeModeActive) {
    xTaskCreatePinnedToCore(logDataTask, "LogDataTask", 8192, NULL, 0, NULL,
                            1);
  } else {
    debugLogger.println(DebugLogger::WARNING,
                        "Safe mode: LogDataTask not started");
  }
#endif
  // Créer une tâche pour vérifier la tension des batteries
  xTaskCreatePinnedToCore(checkBatteryTask, "checkBatteryTask", 4096, NULL, 0,
                          NULL, 1);

#ifdef ENABLE_TIME_AND_INFLUXDB
  // Créer une tâche pour envoyer les données stockées sur la carte SD à
  // InfluxDB
  if (!g_safeModeActive) {
    xTaskCreatePinnedToCore(sendStoredDataTask, "SendStoredDataTask", 65536,
                            NULL, 1, NULL, 0); // 32768
  }

  // Créer une tâche pour gérer le temps et InfluxDB
  if (!g_safeModeActive) {
    xTaskCreatePinnedToCore(timeAndInfluxTask, "TimeAndInfluxTask", 8192,
                            NULL, 1, NULL, 0);
  }

#endif

#if defined(ENABLE_TIME_AND_INFLUXDB) || defined(ENABLE_MQTT)
  if (!g_safeModeActive) {
    wifiHandler.begin(); // Démarrer la connexion WiFi pour cloud/MQTT
  }
#endif

#ifdef ENABLE_MQTT
  if (!g_safeModeActive) {
    xTaskCreatePinnedToCore(mqttTelemetryTask, "MqttTelemetryTask", 8192,
                            NULL, 1, NULL, 0);
  } else {
    debugLogger.println(DebugLogger::WARNING,
                        "Safe mode: MqttTelemetryTask not started");
  }
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
  if (!g_safeModeActive) {
    webServerHandler.begin(); // Démarrer le serveur web
  }

  // Créer une tâche pour gérer le serveur web

  if (!g_safeModeActive) {
    xTaskCreatePinnedToCore(webServerTask, "WebServerTask", 12288, NULL, 1,
                            NULL, 1);
  }

#endif

#ifdef ENABLE_SD_LOGGING
  if (!SD.begin()) {
    debugLogger.println(DebugLogger::SD, "Card Mount Failed");
    return;
  }
  debugLogger.println(DebugLogger::SD, "SD card initialized");
#endif

  // NOTE: timeAndInfluxTask already created above — removed duplicate
}

/**
 * @brief Boucle principale du programme.
 */
void loop() {
  // TASK-005: Safe mode exit strategy after stability window
  // After kBrownoutCounterClearUptimeMs (120s) without new brownout resets,
  // automatically exit safe mode to allow normal operation retry.
  // This prevents firmware from being permanently stuck in safe mode.
  const uint32_t nowMs = millis();
  if (nowMs > kBrownoutCounterClearUptimeMs) {
    if (!g_brownoutCounterCleared) {
      // Clear brownout counter — no new brownouts detected during stability window
      g_brownoutResetCount = 0;
      g_brownoutCounterCleared = true;
      debugLogger.println(DebugLogger::INFO,
                          "Brownout reset counter cleared after stable uptime");
      
      // Exit safe mode to allow task resumption (WiFi, logging, MQTT, etc.)
      if (g_safeModeActive) {
        g_safeModeActive = false;
        debugLogger.println(DebugLogger::WARNING,
                            "Safe mode disabled after 120s stability window — resuming normal operation");
      }
    }
  }

  // Laisser les tâches FreeRTOS gérer les opérations
  vTaskDelay(1000 / portTICK_PERIOD_MS); // Ajouter un léger délai pour
  //  éviter une boucle serrée
}