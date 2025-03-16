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
 * @file WiFiHandler.h
 * @brief Déclaration de la classe WiFiHandler pour gérer la connexion WiFi.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
 */

#ifndef WIFIHANDLER_H
#define WIFIHANDLER_H

#include <WiFi.h>
#include <DebugLogger.h>

/**
 * @class WiFiHandler
 * @brief Classe pour gérer la connexion WiFi.
 */
class WiFiHandler {
public:
    /**
     * @brief Constructeur de la classe WiFiHandler.
     */
    WiFiHandler();

    /**
     * @brief Constructeur de la classe WiFiHandler.
     * @param ssid Le SSID du réseau WiFi.
     * @param password Le mot de passe du réseau WiFi.
     */
    WiFiHandler(const char *ssid, const char *password);

    /**
     * @brief Constructeur de la classe WiFiHandler pour les réseaux sans mot de passe.
     * @param ssid Le SSID du réseau WiFi.
     */
    WiFiHandler(const char *ssid);

    /**
     * @brief Démarrer la connexion WiFi.
     * @param ssid Le SSID du réseau WiFi.
     * @param password Le mot de passe du réseau WiFi.
     */
    void begin(const char* ssid, const char* password);

    /**
     * @brief Démarrer la connexion WiFi.
     */
    void begin();

    /**
     * @brief Vérifier si la connexion WiFi est établie.
     * @return true si connecté, false sinon.
     */
    bool isConnected();

private:
    const char *ssid;
    const char *password;
};

#endif // WIFIHANDLER_H
