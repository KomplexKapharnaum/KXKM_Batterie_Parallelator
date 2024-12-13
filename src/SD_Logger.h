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
#include <SPI.h>

struct CSVConfig {
  char separator;
};

class SDLogger {
public:
  SDLogger();
  /**
   * @brief Initialiser la carte SD et ouvrir le fichier de log.
   * @param filename Le nom du fichier de log.
   * @param config La configuration du CSV.
   */
  void begin(const char *filename, CSVConfig config);

  /**
   * @brief Enregistrer les données sur la carte SD.
   *
   * @param time Le temps actuel.
   * @param bat_nb Le numéro de la batterie.
   * @param volt La tension.
   * @param current Le courant.
   * @param switchState L'état du commutateur.
   * @param ampereHour La consommation en ampère-heure.
   */
  void logData(unsigned long time, int bat_nb, float volt, float current,
               bool switchState, float ampereHour);

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

private:
  const int chipSelect = 10; // Pin de sélection de la carte SD
  File dataFile;
  unsigned long lastLogTime = 0;
  int log_at_time = 10; // Temps entre chaque enregistrement en secondes
  CSVConfig csvConfig;  // Ajouter cette ligne
};

#endif // SDLOGGER_H