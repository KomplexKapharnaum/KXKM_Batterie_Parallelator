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
#include "BatteryRouteValidation.h"
#include "WebMutationRateLimit.h"
#include "WebRouteSecurity.h"
#include "SD_Logger.h"   // Ajouter cette ligne
#include <ArduinoJson.h> // Ajouter cette ligne
#include <SD.h>          // Inclure la bibliothèque SD
#include <SPIFFS.h>      // Ajouter cette ligne
#include <WebSocketsServer.h> // Ajouter cette ligne
#include <KxLogger.h>
#include <WiFi.h> // Ajouter cette ligne
#include "WebServerFiles.h" // Inclure le fichier WebServerFiles.h
#include <ESPAsyncWebServer.h> // Ajouter cette ligne
#include <IPAddress.h>

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern KxLogger debugLogger;
extern INAHandler inaHandler;
extern BATTParallelator BattParallelator;

// Battery protection thresholds (defined in main.cpp)
extern const float set_min_voltage;  // in mV
extern const float set_max_voltage;  // in mV
extern BatteryManager batteryManager;
extern SDLogger sdLogger;
extern String logFilename; // Ajouter une variable globale pour le nom du fichier de log

WebSocketsServer webSocket = WebSocketsServer(81); // Ajouter cette ligne (80 ou 81 ?)
AsyncWebServer server(80); // Remplacer WebServer par AsyncWebServer

#ifndef BMU_WEB_ADMIN_TOKEN
#define BMU_WEB_ADMIN_TOKEN ""
#endif

#ifndef BMU_WEB_ALLOW_LEGACY_GET_MUTATION
#define BMU_WEB_ALLOW_LEGACY_GET_MUTATION 0
#endif

namespace {
const char *kBmuWebAdminToken = BMU_WEB_ADMIN_TOKEN;
constexpr const char *kBmuMutationTokenHeader = "X-BMU-Token";
constexpr const char *kAuthorizationHeader = "Authorization";
constexpr uint8_t kMutationRateLimitMaxRequests = 10;
constexpr uint32_t kMutationRateLimitWindowMs = 10000;

::MutationRateLimitSlot g_rateLimitSlots[8];

uint32_t ipToKey(const IPAddress &ip) {
  return (static_cast<uint32_t>(ip[0]) << 24) |
         (static_cast<uint32_t>(ip[1]) << 16) |
         (static_cast<uint32_t>(ip[2]) << 8) |
         static_cast<uint32_t>(ip[3]);
}

String requestSource(AsyncWebServerRequest *request) {
  if (request != nullptr && request->client() != nullptr) {
    return request->client()->remoteIP().toString();
  }
  return "unknown";
}

void logMutationAudit(const char *route, const String &batteryParam,
                     const String &source, const char *outcome) {
  debugLogger.println(KxLogger::WEB,
                      String("AUDIT route=") + route +
                          " battery=" + batteryParam + " source=" + source +
                          " outcome=" + outcome +
                          " ts=" + String(millis()));
}

bool isMutationRateLimited(AsyncWebServerRequest *request) {
  if (request == nullptr || request->client() == nullptr) {
    return false;
  }

  const uint32_t key = ipToKey(request->client()->remoteIP());
  const uint32_t now = millis();

  return mutationRateLimitExceeded(g_rateLimitSlots, 8, key, now,
                                   kMutationRateLimitMaxRequests,
                                   kMutationRateLimitWindowMs);
}

bool authorizeBatteryMutationRequest(AsyncWebServerRequest *request,
                                     const char *route,
                                     const String &batteryParam) {
  const String source = requestSource(request);

  if (request->method() != HTTP_POST) {
#if BMU_WEB_ALLOW_LEGACY_GET_MUTATION
    if (request->method() != HTTP_GET) {
      logMutationAudit(route, batteryParam, source, "method_not_allowed");
      request->send(405, "text/plain", "Method Not Allowed");
      return false;
    }
    logMutationAudit(route, batteryParam, source, "legacy_get_accepted");
#else
    logMutationAudit(route, batteryParam, source, "method_not_allowed");
    request->send(405, "text/plain", "Method Not Allowed");
    return false;
#endif
  }

  if (isMutationRateLimited(request)) {
    logMutationAudit(route, batteryParam, source, "rate_limited");
    request->send(429, "text/plain", "Too many mutation requests");
    return false;
  }

  if (!isMutationRouteEnabled(kBmuWebAdminToken)) {
    logMutationAudit(route, batteryParam, source, "route_disabled");
    request->send(403, "text/plain",
                  "Battery write routes disabled: configure BMU_WEB_ADMIN_TOKEN");
    return false;
  }

  bool authorized = false;
  if (request->hasHeader(kBmuMutationTokenHeader)) {
    const String tokenHeader = request->header(kBmuMutationTokenHeader);
    authorized =
        isMutationTokenAuthorized(tokenHeader.c_str(), kBmuWebAdminToken);
  } else if (request->hasHeader(kAuthorizationHeader)) {
    const String authorizationHeader = request->header(kAuthorizationHeader);
    authorized = isMutationAuthorizationHeaderAuthorized(
        authorizationHeader.c_str(), kBmuWebAdminToken);
  } else if (request->hasParam("token") || request->hasParam("token", true)) {
    const AsyncWebParameter *tokenParam = request->hasParam("token")
                                            ? request->getParam("token")
                                            : request->getParam("token", true);
    authorized = isMutationTokenAuthorized(tokenParam->value().c_str(),
                                           kBmuWebAdminToken);
  }

  if (!authorized) {
    logMutationAudit(route, batteryParam, source, "unauthorized");
    request->send(403, "text/plain", "Unauthorized battery mutation request");
    return false;
  }

  logMutationAudit(route, batteryParam, source, "authorized");
  return true;
}
} // namespace

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
      server.on("/switch_on", HTTP_POST, std::bind(&WebServerHandler::handleSwitchOn, this, std::placeholders::_1));
      server.on("/switch_off", HTTP_POST, std::bind(&WebServerHandler::handleSwitchOff, this, std::placeholders::_1));
    #if BMU_WEB_ALLOW_LEGACY_GET_MUTATION
      server.on("/switch_on", HTTP_GET, std::bind(&WebServerHandler::handleSwitchOn, this, std::placeholders::_1));
      server.on("/switch_off", HTTP_GET, std::bind(&WebServerHandler::handleSwitchOff, this, std::placeholders::_1));
    #endif
      server.on("/log", HTTP_GET, std::bind(&WebServerHandler::handleLog, this, std::placeholders::_1));
      server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/css", style_css);
      });
      server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "application/javascript", script_js);
      });
      server.onNotFound([](AsyncWebServerRequest *request) { // Corriger la déclaration de la fonction lambda
        debugLogger.println(KxLogger::SPIFF,("File Not Found"));
        request->send(404, "text/plain", "File Not Found");
      });
      server.begin();
      debugLogger.println(KxLogger::WEB,"HTTP server started");
      // Ajouter cette ligne pour envoyer les valeurs automatiquement via WebSocket
      webSocket.begin(); // Initialiser le serveur WebSocket
      webSocket.onEvent(std::bind(&WebServerHandler::onWebSocketEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4)); // Ajouter cette ligne
    } else {
      debugLogger.println(KxLogger::WIFI, "WiFi not connected, cannot start server");
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
      debugLogger.println(KxLogger::WIFI, "WiFi disconnected, server stopped");
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
  String batteryParam = "missing";
  if (request->hasParam("battery")) {
    batteryParam = request->getParam("battery")->value();
  } else if (request->hasParam("battery", true)) {
    batteryParam = request->getParam("battery", true)->value();
  }
  if (!authorizeBatteryMutationRequest(request, "/switch_on", batteryParam)) {
    return;
  }

  if (batteryParam == "missing") {
    logMutationAudit("/switch_on", "missing", requestSource(request),
                     "missing_battery_param");
    request->send(400, "text/plain", "Battery parameter missing");
    return;
  }

  const int nbBatteries = inaHandler.getNbINA();
  int battery = -1;
  if (!parseBatteryIndex(batteryParam.c_str(), nbBatteries, battery)) {
    logMutationAudit("/switch_on", batteryParam,
                     requestSource(request), "invalid_battery_param");
    request->send(400, "text/plain", "Invalid battery parameter");
    return;
  }

  // TASK-007: Voltage precondition check — do not allow switching if voltage is unsafe
  // Prevents switching during brownout, overvoltage, or sensor failure conditions
  if (!validateBatteryVoltageForSwitch(battery, set_min_voltage, set_max_voltage)) {
    logMutationAudit("/switch_on", String(battery), requestSource(request),
                     "voltage_precondition_failed");
    // Use HTTP 423 (Locked) to indicate operation blocked by condition
    request->send(423, "text/plain", 
                  "Cannot switch: battery voltage out of safe range");
    return;
  }

  bool success = BattParallelator.switch_battery(battery, true);
  if (success) {
    logMutationAudit("/switch_on", String(battery), requestSource(request),
                     "switch_success");
    request->send(200, "text/plain", "Switched on battery " + String(battery));
  } else {
    logMutationAudit("/switch_on", String(battery), requestSource(request),
                     "switch_failure");
    request->send(500, "text/plain", "Failed to switch on battery " + String(battery));
  }
}

/**
 * @brief Gérer la requête pour éteindre une batterie.
 */
void WebServerHandler::handleSwitchOff(AsyncWebServerRequest *request) {
  String batteryParam = "missing";
  if (request->hasParam("battery")) {
    batteryParam = request->getParam("battery")->value();
  } else if (request->hasParam("battery", true)) {
    batteryParam = request->getParam("battery", true)->value();
  }
  if (!authorizeBatteryMutationRequest(request, "/switch_off", batteryParam)) {
    return;
  }

  if (batteryParam == "missing") {
    logMutationAudit("/switch_off", "missing", requestSource(request),
                     "missing_battery_param");
    request->send(400, "text/plain", "Battery parameter missing");
    return;
  }

  const int nbBatteries = inaHandler.getNbINA();
  int battery = -1;
  if (!parseBatteryIndex(batteryParam.c_str(), nbBatteries, battery)) {
    logMutationAudit("/switch_off", batteryParam,
                     requestSource(request), "invalid_battery_param");
    request->send(400, "text/plain", "Invalid battery parameter");
    return;
  }

  // TASK-007: Voltage validity check (for consistency, though switch_off is less critical)
  // Still validate that sensor reading is possible and battery exists
  if (!validateBatteryVoltageForSwitch(battery, set_min_voltage, set_max_voltage)) {
    logMutationAudit("/switch_off", String(battery), requestSource(request),
                     "voltage_check_failed");
    // For off, we allow anyway since safe shutdown is often needed during faults
    // Log the issue but continue
  }

  bool success = BattParallelator.switch_battery(battery, false);
  if (success) {
    logMutationAudit("/switch_off", String(battery), requestSource(request),
                     "switch_success");
    request->send(200, "text/plain", "Switched off battery " + String(battery));
  } else {
    logMutationAudit("/switch_off", String(battery), requestSource(request),
                     "switch_failure");
    request->send(500, "text/plain", "Failed to switch off battery " + String(battery));
  }
}

/**
 * @brief Définir l'offset de différence de tension pour la déconnexion de la batterie.
 * @param offset L'offset de différence de tension en volts.
 */
void WebServerHandler::setVoltageOffset(float offset) {
  if (offset < 0.0f) {
    offset = 0.0f;
  }
  // API Web expresses offset in volts, BatteryParallelator stores mV.
  BattParallelator.set_max_diff_voltage(offset * 1000.0f);
}

/**
 * @brief Gérer les requêtes WebSocket pour envoyer les valeurs automatiquement.
 */
void WebServerHandler::handleWebSocket(AsyncWebServerRequest *request) {
  debugLogger.println(KxLogger::INFO, "WebSocket request received");

  // Utiliser ArduinoJson pour générer le JSON
  StaticJsonDocument<1024> doc;
  JsonArray batteryStatus = doc.createNestedArray("batteryStatus");

  for (int i = 0; i < inaHandler.getNbINA(); i++) {
    JsonObject battery = batteryStatus.createNestedObject();
    battery["index"] = i;
    float voltage = NAN;
    float current = NAN;
    const bool ok = inaHandler.read_voltage_current(i, voltage, current);
    battery["voltage"] = ok ? voltage : 0.0f;
    battery["current"] = ok ? current : 0.0f;
    battery["ampereHour"] = batteryManager.getAmpereHourConsumption(i);
    battery["ledStatus"] = BattParallelator.check_battery_status(i) ? "green" : "red";
  }

  String json;
  serializeJson(doc, json);
  debugLogger.println(KxLogger::INFO, "Sending JSON data: " + json);
  request->send(200, "application/json", json); // Utiliser request->send au lieu de server.send
}

void WebServerHandler::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_TEXT) {
    debugLogger.println(KxLogger::INFO, "[" + String(num) + "] Received text: " + String((char*)payload));

    // Utiliser ArduinoJson pour générer le JSON
    StaticJsonDocument<1024> doc;
    JsonArray batteryStatus = doc.createNestedArray("batteryStatus");

    for (int i = 0; i < inaHandler.getNbINA(); i++) {
      JsonObject battery = batteryStatus.createNestedObject();
      battery["index"] = i;
      float voltage = NAN;
      float current = NAN;
      const bool ok = inaHandler.read_voltage_current(i, voltage, current);
      battery["voltage"] = ok ? voltage : 0.0f;
      battery["current"] = ok ? current : 0.0f;
      battery["ampereHour"] = batteryManager.getAmpereHourConsumption(i);
      battery["ledStatus"] = BattParallelator.check_battery_status(i) ? "green" : "red";
    }

    String json;
    serializeJson(doc, json);
    debugLogger.println(KxLogger::INFO, "Sending JSON data: " + json);
    webSocket.sendTXT(num, json); // Envoyer les données via WebSocket
  }
}

/**
 * @brief Gérer les requêtes non trouvées.
 */
void WebServerHandler::handleNotFound(AsyncWebServerRequest *request) {
  String message = "404: Not Found - " + request->url(); // Utiliser request->url() au lieu de server.url()
  debugLogger.println(KxLogger::ERROR, message); // Utiliser KxLogger pour le débogage
  request->send(404, "text/plain", message); // Utiliser request->send au lieu de server.send
}

void WebServerHandler::onNotFound(std::function<void(AsyncWebServerRequest *)> fn) {
  server.onNotFound(fn);
}
