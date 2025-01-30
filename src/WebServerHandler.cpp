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
 * @file WebServerHandler.cpp
 * @brief Implémentation de la classe WebServerHandler pour gérer le serveur
 * web.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
 */

#include "WebServerHandler.h"
#include "SD_Logger.h"   // Ajouter cette ligne
#include <ArduinoJson.h> // Ajouter cette ligne
#include <SD.h>          // Inclure la bibliothèque SD
#include <SPIFFS.h>      // Ajouter cette ligne

extern INAHandler inaHandler;
extern BATTParallelator BattParallelator;
extern BatteryManager batteryManager;
extern SDLogger sdLogger;
extern String
    logFilename; // Ajouter une variable globale pour le nom du fichier de log

/**
 * @file WiFiHandler.cpp
 * @brief Implémentation de la classe WiFiHandler pour gérer la connexion WiFi.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
 */

/**
 * @brief Constructeur de la classe WebServerHandler.
 */
WebServerHandler::WebServerHandler() : server(80) {}

/**
 * @brief Démarrer le serveur web.
 */
void WebServerHandler::begin() {
  server.on("/", HTTP_GET, std::bind(&WebServerHandler::handleRoot, this));
  server.on("/switch_on", HTTP_GET, std::bind(&WebServerHandler::handleSwitchOn, this));
  server.on("/switch_off", HTTP_GET,
            std::bind(&WebServerHandler::handleSwitchOff, this));
  server.on("/log", HTTP_GET, std::bind(&WebServerHandler::handleLog, this));
  server.serveStatic("/", SPIFFS, "/index.html");
  server.serveStatic("/style.css", SPIFFS, "/style.css");
  server.serveStatic("/script.js", SPIFFS, "/script.js");
  server.onNotFound([]() { Serial.println("File Not Found"); });
  server.begin();
  Serial.println("HTTP server started");

  // Ajouter cette ligne pour envoyer les valeurs automatiquement via WebSocket
  server.on("/ws", HTTP_GET, std::bind(&WebServerHandler::handleWebSocket, this));
}

/**
 * @brief Gérer les requêtes des clients.
 */
void WebServerHandler::handleClient() { server.handleClient(); }

/**
 * @brief Gérer la requête pour la racine du serveur.
 */
void WebServerHandler::handleRoot() {
  String html = "<html><head><title>Battery Monitor</title><link rel='stylesheet' type='text/css' href='style.css'></head><body><h1>Battery Monitor</h1><h2>Battery Status</h2><table border='1'><tr><th>Battery</th><th>Voltage (V)</th><th>Current (A)</th><th>Ah</th><th>Status</th><th>Actions</th></tr>";
  
  int Nb_Batt = inaHandler.getNbINA();
  for (int i = 0; i < Nb_Batt; i++) {
    html += "<tr>";
    html += "<td>Battery " + String(i) + "</td>";
    html += "<td>" + String(inaHandler.read_volt(i)) + "</td>";
    html += "<td>" + String(inaHandler.read_current(i)) + "</td>";
    html += "<td>" + String(batteryManager.getAmpereHourConsumption(i)) + "</td>";
    html += "<td><span style='color:" + String(BattParallelator.check_battery_status(i) ? "green" : "red") + ";'>●</span></td>";
    html += "<td><button onclick=\"switchBattery(" + String(i) + ", true)\">Switch On</button> <button onclick=\"switchBattery(" + String(i) + ", false)\">Switch Off</button></td>";
    html += "</tr>";
  }
  html += "</table><h2>Log Data</h2><a href='/log' class='button'>View Log</a></body></html>";
  
  server.send(200, "text/html", html);
}

/**
 * @brief Gérer la requête pour afficher le fichier de log.
 */
void WebServerHandler::handleLog() {
  File logFile =
      SD.open("/" + logFilename +
              ".csv"); // Utiliser le nom de fichier défini dans setup
  if (!logFile) {
    server.send(500, "text/plain", "Failed to open log file");
    return;
  }

  String html = "<html><body><h1>Log Data</h1><table "
                "border='1'><tr><th>Temps</th><th>Numéro de "
                "batterie</th><th>Tension</th><th>Courant</th><th>État du "
                "commutateur</th><th>Consommation en Ah</th></tr>";
  while (logFile.available()) {
    String line = logFile.readStringUntil('\n');
    html += "<tr>";
    int start = 0;
    int end = line.indexOf(sdLogger.getSeparator());
    while (end != -1) {
      html += "<td>" + line.substring(start, end) + "</td>";
      start = end + 1;
      end = line.indexOf(sdLogger.getSeparator(), start);
    }
    html += "<td>" + line.substring(start) + "</td>";
    html += "</tr>";
  }
  html += "</table></body></html>";
  server.send(200, "text/html", html);
  logFile.close();
}

/**
 * @brief Gérer la requête pour allumer une batterie.
 */
void WebServerHandler::handleSwitchOn() {
  if (server.hasArg("battery")) {
    int battery = server.arg("battery").toInt();
    bool success = BattParallelator.switch_battery(battery, true);
    if (success) {
      server.send(200, "text/plain", "Switched on battery " + String(battery));
    } else {
      server.send(500, "text/plain", "Failed to switch on battery " + String(battery));
    }
  } else {
    server.send(400, "text/plain", "Battery parameter missing");
  }
}

/**
 * @brief Gérer la requête pour éteindre une batterie.
 */
void WebServerHandler::handleSwitchOff() {
  if (server.hasArg("battery")) {
    int battery = server.arg("battery").toInt();
    bool success = BattParallelator.switch_battery(battery, false);
    if (success) {
      server.send(200, "text/plain", "Switched off battery " + String(battery));
    } else {
      server.send(500, "text/plain", "Failed to switch off battery " + String(battery));
    }
  } else {
    server.send(400, "text/plain", "Battery parameter missing");
  }
}

/**
 * @brief Définir l'offset de différence de tension pour la déconnexion de la
 * batterie.
 * @param offset L'offset de différence de tension en volts.
 */
void WebServerHandler::setVoltageOffset(float offset) {
  BattParallelator.set_max_diff_voltage(offset);
}

/**
 * @brief Gérer les requêtes WebSocket pour envoyer les valeurs automatiquement.
 */
void WebServerHandler::handleWebSocket() {
  // Envoyer les valeurs automatiquement via WebSocket
  String json = "{";
  json += "\"batteryStatus\":[";
  for (int i = 0; i < inaHandler.getNbINA(); i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"index\":" + String(i) + ",";
    json += "\"voltage\":" + String(inaHandler.read_volt(i)) + ",";
    json += "\"current\":" + String(inaHandler.read_current(i)) + ",";
    json += "\"ampereHour\":" + String(batteryManager.getAmpereHourConsumption(i)) + ",";
    json += "\"ledStatus\":\"" + String(BattParallelator.check_battery_status(i) ? "green" : "red") + "\"";
    json += "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}
