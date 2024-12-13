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
 * @file WebServerHandler.h
 * @brief Déclaration de la classe WebServerHandler pour gérer le serveur web.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
 */

#ifndef WEBSERVERHANDLER_H
#define WEBSERVERHANDLER_H

#include <WiFi.h>
#include <WebServer.h>
#include "Batt_Parallelator_lib.h"

/**
 * @class WebServerHandler
 * @brief Classe pour gérer le serveur web.
 */
class WebServerHandler {
public:
    /**
     * @brief Constructeur de la classe WebServerHandler.
     * @param ssid Le SSID du réseau WiFi.
     * @param password Le mot de passe du réseau WiFi.
     */
    WebServerHandler(const char* ssid, const char* password);

    /**
     * @brief Démarrer le serveur web.
     */
    void begin();

    /**
     * @brief Gérer les requêtes des clients.
     */
    void handleClient();

    /**
     * @brief Définir l'offset de différence de tension pour la déconnexion de la batterie.
     * @param offset L'offset de différence de tension en volts.
     */
    void setVoltageOffset(float offset);

private:
    /**
     * @brief Gérer la requête de la page d'accueil.
     */
    void handleRoot();

    /**
     * @brief Gérer la requête pour allumer une batterie.
     */
    void handleSwitchOn();

    /**
     * @brief Gérer la requête pour éteindre une batterie.
     */
    void handleSwitchOff();

    /**
     * @brief Gérer la requête pour afficher le fichier de log.
     */
    void handleLog();

    WebServer server; ///< Instance du serveur web.
    const char* ssid; ///< SSID du réseau WiFi.
    const char* password; ///< Mot de passe du réseau WiFi.
};

#endif // WEBSERVERHANDLER_H
