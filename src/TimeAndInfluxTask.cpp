#include "TimeAndInfluxTask.h"
#include "InfluxDBHandler.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WiFi.h>
#include <ESP_SSLClient.h>
#include <SD.h>
#include <time.h>
#include <DebugLogger.h>

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern DebugLogger debugLogger;
extern NTPClient timeClient;
extern InfluxDBHandler influxDBHandler;
extern bool isTimeSynced;
extern void sendStoredDataTask(void *pvParameters);

/**
 * @brief Mettre à jour la RTC avec la date et l'heure de compilation.
 */
void updateRTCWithCompileTime() {
  struct tm compileTime;
  strptime(__DATE__ " " __TIME__, "%b %d %Y %H:%M:%S", &compileTime);
  time_t t = mktime(&compileTime);
  struct timeval now = { .tv_sec = t };
  settimeofday(&now, NULL);
  debugLogger.println(DebugLogger::TIME, "RTC updated with compile time");
}

/**
 * @brief Tâche pour gérer la synchronisation du temps et le démarrage de InfluxDB.
 * @param pvParameters Paramètres de la tâche.
 */
void timeAndInfluxTask(void *pvParameters) {
  // Mettre à jour la RTC avec la date et l'heure de compilation
  updateRTCWithCompileTime();
  debugLogger.println(DebugLogger::TIME, "RTC updated with compile time");

  while (true) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!isTimeSynced) {
        timeClient.begin();
        timeClient.setTimeOffset(3600); // Définir le décalage horaire en secondes (ex: 3600 pour GMT+1)
        debugLogger.println(DebugLogger::TIME, "NTP client started");

        // Synchroniser l'heure avec NTP
        timeClient.update();
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
          isTimeSynced = true;
          debugLogger.println(DebugLogger::TIME, "Time synchronized with NTP");

          // Mettre à jour la RTC avec l'heure NTP
          time_t t = timeClient.getEpochTime();
          struct timeval now = { .tv_sec = t };
          settimeofday(&now, NULL);
          debugLogger.println(DebugLogger::TIME, "RTC updated with NTP time");

          // Désactiver la demande de mise à jour NTP du temps
          timeClient.end();
        } else {
          debugLogger.println(DebugLogger::WARNING, "Failed to synchronize time with NTP, using RTC");
        }
      }

      if (!SD.begin()) {
        debugLogger.println(DebugLogger::ERROR, "Card Mount Failed");
        return;
      }
      debugLogger.println(DebugLogger::INFO, "SD card initialized");

      influxDBHandler.begin();
      debugLogger.println(DebugLogger::INFO, "InfluxDB handler initialized");

    } else {
      debugLogger.println(DebugLogger::WARNING, "WiFi not connected, unable to synchronize time with NTP or connect to InfluxDB");
      isTimeSynced = false; // Reset time sync status
    }

    // Afficher la date et l'heure actuelles
    if (isTimeSynced) {
      timeClient.update();
      debugLogger.println(DebugLogger::TIME, "Current Date and Time: " + timeClient.getFormattedTime());
    } else {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        char buffer[20];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
        debugLogger.println(DebugLogger::TIME, "=================================== > Current Date and Time: " + String(buffer));
      } else {
        debugLogger.println(DebugLogger::ERROR, "RTC time not available, unable to display date and time");
      }
    }

    vTaskDelay(60000 / portTICK_PERIOD_MS); // Attendre 1 minute avant de réessayer
  }
}
