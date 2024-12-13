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
 * @brief Implémentation de la classe WebServerHandler pour gérer le serveur web.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
 */

#include "WebServerHandler.h"
#include <SD.h> // Inclure la bibliothèque SD
#include "SD_Logger.h" // Ajouter cette ligne

extern INAHandler inaHandler;
extern BATTParallelator BattParallelator;
extern BatteryManager batteryManager;
extern SDLogger sdLogger;
extern String logFilename; // Ajouter une variable globale pour le nom du fichier de log

/**
 * @brief Constructeur de la classe WebServerHandler.
 * @param ssid Le SSID du réseau WiFi.
 * @param password Le mot de passe du réseau WiFi.
 */
WebServerHandler::WebServerHandler(const char* ssid, const char* password)
    : server(80), ssid(ssid), password(password) {}

/**
 * @brief Démarrer le serveur web.
 */
void WebServerHandler::begin() {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");

    server.on("/", std::bind(&WebServerHandler::handleRoot, this));
    server.on("/switch_on", std::bind(&WebServerHandler::handleSwitchOn, this));
    server.on("/switch_off", std::bind(&WebServerHandler::handleSwitchOff, this));
    server.on("/log", std::bind(&WebServerHandler::handleLog, this));
    server.begin();
    Serial.println("HTTP server started");
}

/**
 * @brief Gérer les requêtes des clients.
 */
void WebServerHandler::handleClient() {
    server.handleClient();
}

/**
 * @brief Gérer la requête de la page d'accueil.
 */
void WebServerHandler::handleRoot() {
    String html = "<html><body><h1>Battery Monitor</h1>";
    html += "<h2>Battery Status</h2><ul>";
    int Nb_Batt = inaHandler.getNbINA();
    for (int i = 0; i < Nb_Batt; i++) {
        bool isOn = BattParallelator.check_battery_status(i);
        String ledStatus = isOn ? "green" : "red";
        html += "<li>Battery " + String(i) + ": Voltage = " + String(inaHandler.read_volt(i)) + "V, Current = " + String(inaHandler.read_current(i)) + "A, Ah = " + String(batteryManager.getAmpereHourConsumption(i)) + " <span style=\"color:" + ledStatus + ";\">●</span></li>";
    }
    html += "</ul>";
    html += "<h2>Control Switches</h2><ul>";
    for (int i = 0; i < Nb_Batt; i++) {
        html += "<li>Battery " + String(i) + ": <a href=\"/switch_on?battery=" + String(i) + "\">Switch On</a> | <a href=\"/switch_off?battery=" + String(i) + "\">Switch Off</a></li>";
    }
    html += "</ul>";
    html += "<h2>Log Data</h2><a href=\"/log\">View Log</a>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

/**
 * @brief Gérer la requête pour afficher le fichier de log.
 */
void WebServerHandler::handleLog() {
    File logFile = SD.open("/" + logFilename + ".csv"); // Utiliser le nom de fichier défini dans setup
    if (!logFile) {
        server.send(500, "text/plain", "Failed to open log file");
        return;
    }

    String html = "<html><body><h1>Log Data</h1><table border='1'><tr><th>Temps</th><th>Numéro de batterie</th><th>Tension</th><th>Courant</th><th>État du commutateur</th><th>Consommation en Ah</th></tr>";
    while (logFile.available()) {
        String line = logFile.readStringUntil('\n');
        html += "<tr>";
        int start = 0;
        int end = line.indexOf(',');
        while (end != -1) {
            html += "<td>" + line.substring(start, end) + "</td>";
            start = end + 1;
            end = line.indexOf(',', start);
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
        BattParallelator.switch_battery(battery, true);
        server.send(200, "text/plain", "Switched on battery " + String(battery));
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
        BattParallelator.switch_battery(battery, false);
        server.send(200, "text/plain", "Switched off battery " + String(battery));
    } else {
        server.send(400, "text/plain", "Battery parameter missing");
    }
}

/**
 * @brief Définir l'offset de différence de tension pour la déconnexion de la batterie.
 * @param offset L'offset de différence de tension en volts.
 */
void WebServerHandler::setVoltageOffset(float offset) {
    BattParallelator.set_max_diff_voltage(offset);
}
