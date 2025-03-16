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
 * @file SD_Logger.cpp
 * @brief Implémentation de la classe SDLogger pour gérer l'enregistrement des
 * données sur une carte SD.
 */

#include "SD_Logger.h"
#include <DebugLogger.h>
#include <SPI.h>

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern DebugLogger debugLogger;

// Ajouter la définition du constructeur
SDLogger::SDLogger() : csvConfig{','}, filename(nullptr) {}

/**
 * @brief Initialiser la carte SD et ouvrir le fichier de log.
 * @param filename Le nom du fichier de log.
 * @param config La configuration du CSV.
 */
void SDLogger::begin(const char *filename, CSVConfig config) {
  this->filename = filename;
  csvConfig = config;
  SPI.begin(18, 5, 19); // Initialiser le bus SPI
  if (!SD.begin(chipSelect)) {
    debugLogger.printlnDebug(DebugLogger::ERROR, "Échec de l'initialisation de la carte SD !");
    return;
  }
  String fullFilename = String(filename) + ".csv";
  if (SD.exists(fullFilename.c_str())) {
    dataFile = SD.open(fullFilename.c_str(), FILE_APPEND); // Ouvrir en mode ajout
    if (dataFile) {
      dataFile.println("Temps" + String(csvConfig.separator) +
                       "Numéro de batterie" + String(csvConfig.separator) +
                       "Tension" + String(csvConfig.separator) + "Courant" +
                       String(csvConfig.separator) + "État du commutateur" +
                       String(csvConfig.separator) + "Consommation en Ah");
      //dataFile.close(); // Assurez-vous de fermer le fichier après l'avoir créé
    } else {
      debugLogger.printlnDebug(DebugLogger::ERROR, "Échec de l'ouverture de " + fullFilename + " pour l'ajout !");
    }
  } else {
    dataFile = SD.open(fullFilename.c_str(), FILE_WRITE); // Ouvrir en mode écriture
    if (dataFile) {
      dataFile.println("Temps" + String(csvConfig.separator) +
                       "Numéro de batterie" + String(csvConfig.separator) +
                       "Tension" + String(csvConfig.separator) + "Courant" +
                       String(csvConfig.separator) + "État du commutateur" +
                       String(csvConfig.separator) + "Consommation en Ah");
      //dataFile.close(); // Assurez-vous de fermer le fichier après l'avoir créé
    } else {
      debugLogger.printlnDebug(DebugLogger::ERROR, "Échec de l'ouverture de " + fullFilename + " pour l'écriture !");
    }
  }
  if (!dataFile) {
    debugLogger.printlnDebug(DebugLogger::ERROR, "Échec de l'ouverture de " + fullFilename +
                             " pour l'écriture !");
  }
}

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
void SDLogger::logData(const char *time, int bat_nb, float volt, float current,
                       bool switchState, float ampereHour) {
  String fullFilename = String(filename) + ".csv";
  dataFile = SD.open(fullFilename.c_str(), FILE_APPEND);
  if (dataFile && volt > 1) { // Enregistrer uniquement les batteries connectées
    dataFile.print(time);
    dataFile.print(csvConfig.separator);
    dataFile.print(bat_nb);
    dataFile.print(csvConfig.separator);
    dataFile.print(volt);
    dataFile.print(csvConfig.separator);
    dataFile.print(current);
    dataFile.print(csvConfig.separator);
    dataFile.print(switchState ? "ON" : "OFF");
    dataFile.print(csvConfig.separator);
    dataFile.println(ampereHour);
    dataFile.flush();
    //dataFile.close();
  } else {
    debugLogger.printlnDebug(DebugLogger::ERROR, "Erreur d'écriture dans le fichier de log");
  }
}

/**
 * @brief Vérifier s'il est temps d'enregistrer les données.
 *
 * @return true s'il est temps d'enregistrer les données, false sinon.
 */
bool SDLogger::shouldLog() {
  unsigned long currentTime = millis();
  if (currentTime - lastLogTime >= log_at_time) { // 10 secondes
    lastLogTime = currentTime;
    return true;
  }
  return false;
}

/**
 * @brief Définir l'intervalle de temps d'enregistrement.
 *
 * @param time L'intervalle de temps d'enregistrement en secondes.
 */
void SDLogger::setLogTime(int time) // définir le temps de log en secondes
{
  log_at_time = time * 1000;
}
