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
#include <WebSocketsServer.h> // Ajouter cette ligne
#include <DebugLogger.h>
#include <WiFi.h> // Ajouter cette ligne
#include "WebServerFiles.h" // Inclure le fichier WebServerFiles.h
#include <ESPAsyncWebServer.h> // Ajouter cette ligne

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern DebugLogger debugLogger;
extern INAHandler inaHandler;
extern BATTParallelator BattParallelator;
extern BatteryManager batteryManager;
extern SDLogger sdLogger;
extern String logFilename; // Ajouter une variable globale pour le nom du fichier de log

WebSocketsServer webSocket = WebSocketsServer(81); // Ajouter cette ligne (80 ou 81 ?)
AsyncWebServer server(80); // Remplacer WebServer par AsyncWebServer

/**
 * @brief Constructeur de la classe WebServerHandler.
 */
WebServerHandler::WebServerHandler() : server(80) { // Initialiser AsyncWebServer avec le port 80
  // Initialisation supplémentaire si nécessaire
}

/**
 * @brief Démarrer le serveur web.
 */
void WebServerHandler::begin() {
  try {
    if (WiFi.status() == WL_CONNECTED) { // Vérifier si le WiFi est connecté
      delay (1000);
      server.on("/", HTTP_GET, std::bind(&WebServerHandler::handleRoot, this, std::placeholders::_1));
      server.on("/switch_on", HTTP_GET, std::bind(&WebServerHandler::handleSwitchOn, this, std::placeholders::_1));
      server.on("/switch_off", HTTP_GET, std::bind(&WebServerHandler::handleSwitchOff, this, std::placeholders::_1));
      server.on("/log", HTTP_GET, std::bind(&WebServerHandler::handleLog, this, std::placeholders::_1));
      server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/css", style_css);
      });
      server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "application/javascript", script_js);
      });
      server.onNotFound([](AsyncWebServerRequest *request) { // Corriger la déclaration de la fonction lambda
        debugLogger.println(DebugLogger::SPIFF,("File Not Found"));
        request->send(404, "text/plain", "File Not Found");
      });
      server.begin();
      debugLogger.println(DebugLogger::WEB,"HTTP server started");
      // Ajouter cette ligne pour envoyer les valeurs automatiquement via WebSocket
      webSocket.begin(); // Initialiser le serveur WebSocket
      webSocket.onEvent(std::bind(&WebServerHandler::onWebSocketEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4)); // Ajouter cette ligne
    } else {
      debugLogger.println(DebugLogger::WIFI, "WiFi not connected, cannot start server");
    }
  } catch (const std::exception& e) {
    Serial.print("Exception caught in WebServerHandler::begin: ");
    Serial.println(e.what());
    abort();
  }
}

/**
 * @brief Gérer les requêtes des clients.
 */
void WebServerHandler::handleClient() {
  if (WiFi.status() == WL_CONNECTED) { // Vérifier si le WiFi est connecté
    webSocket.loop(); // Ajouter cette ligne pour gérer les événements WebSocket
  } else {
    if (WiFi.status() != WL_CONNECTED) { // Vérifier si le serveur est en cours d'exécution
      webSocket.close(); // Fermer le WebSocket
      debugLogger.println(DebugLogger::WIFI, "WiFi disconnected, server stopped");
    }
  }
}

void WebServerHandler::handleRoot(AsyncWebServerRequest *request) {
  request->send_P(200, "text/html", index_html);
}

/**
 * @brief Gérer la requête pour afficher le fichier de log.
 */
void WebServerHandler::handleLog(AsyncWebServerRequest *request) {
  File logFile = SD.open("/" + logFilename + ".csv"); // Utiliser le nom de fichier défini dans setup
  if (!logFile) {
    request->send(500, "text/plain", "Failed to open log file");
    return;
  }

  String html = "<html><body><h1>Log Data</h1><table border='1'><tr><th>Temps</th><th>Numéro de batterie</th><th>Tension</th><th>Courant</th><th>État du commutateur</th><th>Consommation en Ah</th></tr>";
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
  request->send(200, "text/html", html);
  logFile.close();
}

/**
 * @brief Gérer la requête pour allumer une batterie.
 */
void WebServerHandler::handleSwitchOn(AsyncWebServerRequest *request) {
  if (request->hasArg("battery")) {
    int battery = request->arg("battery").toInt();
    bool success = BattParallelator.switch_battery(battery, true);
    if (success) {
      request->send(200, "text/plain", "Switched on battery " + String(battery));
    } else {
      request->send(500, "text/plain", "Failed to switch on battery " + String(battery));
    }
  } else {
    request->send(400, "text/plain", "Battery parameter missing");
  }
}

/**
 * @brief Gérer la requête pour éteindre une batterie.
 */
void WebServerHandler::handleSwitchOff(AsyncWebServerRequest *request) {
  if (request->hasArg("battery")) {
    int battery = request->arg("battery").toInt();
    bool success = BattParallelator.switch_battery(battery, false);
    if (success) {
      request->send(200, "text/plain", "Switched off battery " + String(battery));
    } else {
      request->send(500, "text/plain", "Failed to switch off battery " + String(battery));
    }
  } else {
    request->send(400, "text/plain", "Battery parameter missing");
  }
}

/**
 * @brief Définir l'offset de différence de tension pour la déconnexion de la batterie.
 * @param offset L'offset de différence de tension en volts.
 */
void WebServerHandler::setVoltageOffset(float offset) {
  BattParallelator.set_max_diff_voltage(offset);
}

/**
 * @brief Gérer les requêtes WebSocket pour envoyer les valeurs automatiquement.
 */
void WebServerHandler::handleWebSocket(AsyncWebServerRequest *request) {
  debugLogger.println(DebugLogger::INFO, "WebSocket request received");

  // Utiliser ArduinoJson pour générer le JSON
  StaticJsonDocument<1024> doc;
  JsonArray batteryStatus = doc.createNestedArray("batteryStatus");

  for (int i = 0; i < inaHandler.getNbINA(); i++) {
    JsonObject battery = batteryStatus.createNestedObject();
    battery["index"] = i;
    battery["voltage"] = inaHandler.read_volt(i);
    battery["current"] = inaHandler.read_current(i);
    battery["ampereHour"] = batteryManager.getAmpereHourConsumption(i);
    battery["ledStatus"] = BattParallelator.check_battery_status(i) ? "green" : "red";
  }

  String json;
  serializeJson(doc, json);
  debugLogger.println(DebugLogger::INFO, "Sending JSON data: " + json);
  request->send(200, "application/json", json); // Utiliser request->send au lieu de server.send
}

void WebServerHandler::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_TEXT) {
    debugLogger.println(DebugLogger::INFO, "[" + String(num) + "] Received text: " + String((char*)payload));

    // Utiliser ArduinoJson pour générer le JSON
    StaticJsonDocument<1024> doc;
    JsonArray batteryStatus = doc.createNestedArray("batteryStatus");

    for (int i = 0; i < inaHandler.getNbINA(); i++) {
      JsonObject battery = batteryStatus.createNestedObject();
      battery["index"] = i;
      battery["voltage"] = inaHandler.read_volt(i);
      battery["current"] = inaHandler.read_current(i);
      battery["ampereHour"] = batteryManager.getAmpereHourConsumption(i);
      battery["ledStatus"] = BattParallelator.check_battery_status(i) ? "green" : "red";
    }

    String json;
    serializeJson(doc, json);
    debugLogger.println(DebugLogger::INFO, "Sending JSON data: " + json);
    webSocket.sendTXT(num, json); // Envoyer les données via WebSocket
  }
}

/**
 * @brief Gérer les requêtes non trouvées.
 */
void WebServerHandler::handleNotFound(AsyncWebServerRequest *request) {
  String message = "404: Not Found - " + request->url(); // Utiliser request->url() au lieu de server.url()
  debugLogger.println(DebugLogger::ERROR, message); // Utiliser DebugLogger pour le débogage
  request->send(404, "text/plain", message); // Utiliser request->send au lieu de server.send
}

void WebServerHandler::onNotFound(std::function<void(AsyncWebServerRequest *)> fn) {
  server.onNotFound(fn);
}
