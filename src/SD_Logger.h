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
 * @file SD_Logger.h
 * @brief Déclaration de la classe SDLogger pour gérer l'enregistrement des données sur une carte SD.
 */

#ifndef SDLOGGER_H
#define SDLOGGER_H

#include <SD.h>

#define MAX_BATTERIES 16 // À ajuster selon votre besoin

struct CSVConfig {
  char separator;
};

class SDLogger {
public:
  SDLogger();
  ~SDLogger(); // Ajout du destructeur
  /**
   * @brief Initialiser la carte SD et ouvrir le fichier de log.
   * @param filename Le nom du fichier de log.
   * @param config La configuration du CSV.
   */
  void begin(const char *filename, CSVConfig config);

  /**
   * @brief Stocke les données d'une batterie pour un instant donné.
   *
   * @param time Le temps actuel.
   * @param bat_nb Le numéro de la batterie.
   * @param volt La tension.
   * @param current Le courant.
   * @param switchState L'état du commutateur.
   * @param ampereHourConsumption La consommation en ampère-heure (décharge).
   * @param ampereHourCharge La charge en ampère-heure.
   * @param totalConsumption La consommation totale en ampère-heure de toutes les batteries.
   * @param totalCharge La charge totale en ampère-heure de toutes les batteries.
   * @param totalCurrent Le courant total instantané de toutes les batteries.
   */
  void logData(const char *time, int bat_nb, float volt, float current,
               bool switchState, float ampereHourConsumption, float ampereHourCharge,
               float totalConsumption, float totalCharge, float totalCurrent);

  /**
   * @brief Écrit la ligne complète dans le fichier (à appeler après avoir loggé toutes les batteries).
   */
  void flushLine();

  /**
   * @brief À appeler au début pour définir le nombre de batteries.
   *
   * @param count Le nombre de batteries.
   */
  void setBatteryCount(int count);

  /**
   * @brief Vérifier s'il est temps d'enregistrer les données.
   *
   * @return true s'il est temps d'enregistrer les données, false sinon.
   */
  bool shouldLog();

  /**
   * @brief Définir l'intervalle de temps d'enregistrement.
   *
   * @param time L'intervalle de temps d'enregistrement en secondes.
   */
  void setLogTime(int time);

  char getSeparator() const { return csvConfig.separator; }
  String getCurrentTime(const char* logFileBase); // Modifié pour prendre un argument

private:
  const int chipSelect = 21; // Pin de sélection de la carte SD
  File dataFile;
  unsigned long lastLogTime = 0;
  int log_at_time = 10; // Temps entre chaque enregistrement en secondes
  CSVConfig csvConfig;  // Ajouter cette ligne
  char *filename = nullptr; // Modifié pour être un pointeur modifiable
  int findNextFileNumber(const char *baseFilename);
  int batteryCount = 16; // Par défaut 4 batteries
  struct BatteryData {
    float volt;
    float current;
    bool switchState;
    float ampereHourConsumption;
    float ampereHourCharge;
    bool valid;
  };
    float totalConsumption;
    float totalCharge;
    float totalCurrent;

  BatteryData batteryBuffer[MAX_BATTERIES];
  char lastTime[32]; // Pour stocker le timestamp courant
  time_t getLastLogTimeFromSD(const char* logFileBase); // Ajouté
};

#endif // SDLOGGER_H