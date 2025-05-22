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
 * @file WiFiHandler.cpp
 * @brief Implémentation de la classe WiFiHandler pour gérer la connexion WiFi.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
 */

#include "WiFiHandler.h"
#include <DebugLogger.h>

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern DebugLogger debugLogger;

/**
 * @brief Constructeur de la classe WiFiHandler.
 * @param ssid Le SSID du réseau WiFi.
 * @param password Le mot de passe du réseau WiFi.
 */
WiFiHandler::WiFiHandler(const char *ssid, const char *password)
    : ssid(ssid), password(password) {}

/**
 * @brief Constructeur de la classe WiFiHandler pour les réseaux sans mot de passe.
 * @param ssid Le SSID du réseau WiFi.
 */
WiFiHandler::WiFiHandler(const char *ssid)
    : ssid(ssid), password(NULL) {}

/**
 * @brief Démarrer la connexion WiFi.
 */
void WiFiHandler::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("BatteryMonitor");

  if (password) {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
  }
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(1000);
    debugLogger.println(DebugLogger::WIFI,"Connecting to WiFi...");
  }
  debugLogger.println(DebugLogger::WIFI,"=======================> Connected to WiFi");
  debugLogger.println(DebugLogger::WIFI,WiFi.localIP().toString());
}

WiFiHandler::WiFiHandler() {}

void WiFiHandler::begin(const char* ssid, const char* password) {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");
}

bool WiFiHandler::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}
