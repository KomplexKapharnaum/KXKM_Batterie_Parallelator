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
SDLogger::SDLogger() : csvConfig{';'}, filename(nullptr) {
  batteryCount = 4;
  for (int i = 0; i < MAX_BATTERIES; ++i) batteryBuffer[i].valid = false;
  lastTime[0] = '\0';
}

SDLogger::~SDLogger() {
  if (filename != nullptr) {
    free(filename);
    filename = nullptr;
  }
  if (dataFile) {
    dataFile.close();
  }
}

/**
 * @brief Initialiser la carte SD et ouvrir le fichier de log.
 * @param baseFilename Le nom de base du fichier de log.
 * @param config La configuration du CSV.
 */
void SDLogger::begin(const char *baseFilename, CSVConfig config) {
  this->csvConfig = config;
  SPI.begin(18, 5, 19); // Initialiser le bus SPI

  if (!SD.begin(chipSelect)) {
    debugLogger.println(DebugLogger::ERROR, "Échec de l'initialisation de la carte SD !");
    return;
  }

  // Trouver le prochain numéro de fichier disponible
  int fileNumber = findNextFileNumber(baseFilename);

  // Construire le nom de fichier complet avec numéro et s'assurer qu'il commence par '/'
  char tempFilename[64];
  sprintf(tempFilename, "/%s_%03d.csv", baseFilename, fileNumber);

  if (this->filename != nullptr) {
    free(this->filename);
  }
  this->filename = strdup(tempFilename);

  dataFile = SD.open(this->filename, FILE_WRITE);

  if (dataFile) {
    // Entête réorganisée par type de mesure
    dataFile.print("Temps");
    // Voltages
    for (int i = 0; i < batteryCount; ++i) {
      dataFile.print(csvConfig.separator);
      dataFile.print("Volt_"); dataFile.print(i+1);
    }
    // Currents
    for (int i = 0; i < batteryCount; ++i) {
      dataFile.print(csvConfig.separator);
      dataFile.print("Current_"); dataFile.print(i+1);
    }
    // Switches
    for (int i = 0; i < batteryCount; ++i) {
      dataFile.print(csvConfig.separator);
      dataFile.print("Switch_"); dataFile.print(i+1);
    }
    // AhCons
    for (int i = 0; i < batteryCount; ++i) {
      dataFile.print(csvConfig.separator);
      dataFile.print("AhCons_"); dataFile.print(i+1);
    }
    // AhCharge
    for (int i = 0; i < batteryCount; ++i) {
      dataFile.print(csvConfig.separator);
      dataFile.print("AhCharge_"); dataFile.print(i+1);
    }
    // TotCurrent, TotCharge, TotCons à la fin (ordre demandé)
    dataFile.print(csvConfig.separator);
    dataFile.print("TotCurrent");
    dataFile.print(csvConfig.separator);
    dataFile.print("TotCharge");
    dataFile.print(csvConfig.separator);
    dataFile.print("TotCons");
    dataFile.println();
    dataFile.flush();
    debugLogger.println(DebugLogger::INFO, "Fichier de log créé: " + String(this->filename));
  } else {
    debugLogger.println(DebugLogger::ERROR, "Échec de l'ouverture de " + String(this->filename) + " pour l'écriture !");
  }
}

/**
 * @brief Trouver le prochain numéro de fichier disponible.
 * @param baseFilename Le nom de base du fichier de log.
 * @return Le prochain numéro de fichier disponible.
 */
int SDLogger::findNextFileNumber(const char *baseFilename) {
  int maxNumber = 0;
  File root = SD.open("/");

  if (!root) {
    debugLogger.println(DebugLogger::ERROR, "Impossible d'ouvrir le répertoire racine");
    return 1; // Par défaut, commencer à 1 en cas d'erreur
  }

  while (true) {
    File entry = root.openNextFile();
    if (!entry) {
      break; // Plus de fichiers
    }

    String fileName = String(entry.name());
    entry.close();
    
    // Format recherché: baseFilename_XXX.csv
    String baseFileString = String(baseFilename);
    if (fileName.indexOf(baseFileString + "_") >= 0 && fileName.endsWith(".csv")) {
      // Extraire le numéro entre baseFilename_ et .csv
      int underscorePos = fileName.indexOf(baseFileString + "_") + baseFileString.length() + 1;
      int dotPos = fileName.lastIndexOf(".csv");
      
      if (dotPos > underscorePos) {
        String numStr = fileName.substring(underscorePos, dotPos);
        int num = numStr.toInt();
        if (num > maxNumber) {
          maxNumber = num;
        }
      }
    }
  }

  root.close();
  debugLogger.println(DebugLogger::INFO, "Prochain numéro de fichier: " + String(maxNumber + 1));
  return maxNumber + 1;
}

// Stocke les données d'une batterie dans le buffer
void SDLogger::logData(const char *time, int bat_nb, float volt, float current,
                      bool switchState, float ampereHourConsumption, float ampereHourCharge,
                      float totalConsumption, float totalCharge, float totalCurrent) {
  if (bat_nb < 0 || bat_nb >= batteryCount) return;
  batteryBuffer[bat_nb].volt = volt;
  batteryBuffer[bat_nb].current = current;
  batteryBuffer[bat_nb].switchState = switchState;
  batteryBuffer[bat_nb].ampereHourConsumption = ampereHourConsumption;
  batteryBuffer[bat_nb].ampereHourCharge = ampereHourCharge;
  batteryBuffer[bat_nb].valid = true;
  // Stocke le temps pour la ligne
  strncpy(lastTime, time, sizeof(lastTime)-1);
  lastTime[sizeof(lastTime)-1] = '\0';
}

// Écrit la ligne complète dans le fichier CSV
void SDLogger::flushLine() {
  // Vérifie que toutes les batteries sont prêtes
  for (int i = 0; i < batteryCount; ++i) {
    if (!batteryBuffer[i].valid) return; // Pas prêt
  }
  dataFile = SD.open(filename, FILE_APPEND);
  if (dataFile) {
    dataFile.print(lastTime);
    // Voltages
    for (int i = 0; i < batteryCount; ++i) {
      dataFile.print(csvConfig.separator);
      dataFile.print(batteryBuffer[i].volt);
    }
    // Currents
    for (int i = 0; i < batteryCount; ++i) {
      dataFile.print(csvConfig.separator);
      dataFile.print(batteryBuffer[i].current);
    }
    // Switches
    for (int i = 0; i < batteryCount; ++i) {
      dataFile.print(csvConfig.separator);
      dataFile.print(batteryBuffer[i].switchState ? "ON" : "OFF");
    }
    // AhCons
    for (int i = 0; i < batteryCount; ++i) {
      dataFile.print(csvConfig.separator);
      dataFile.print(batteryBuffer[i].ampereHourConsumption, 4);
    }
    // AhCharge
    for (int i = 0; i < batteryCount; ++i) {
      dataFile.print(csvConfig.separator);
      dataFile.print(batteryBuffer[i].ampereHourCharge, 4);
    }
    // TotCurrent, TotCharge, TotCons à la fin (ordre demandé)
    dataFile.print(csvConfig.separator);
    dataFile.print(totalCurrent, 1);
    dataFile.print(csvConfig.separator);
    dataFile.print(totalCharge, 1);
    dataFile.print(csvConfig.separator);
    dataFile.print(totalConsumption, 1);
    
    dataFile.println();
    dataFile.flush();
    dataFile.close();

    // Log de débogage explicite (même ordre)
    debugLogger.print(DebugLogger::SD, "Données ligne CSV: ");
    debugLogger.print(DebugLogger::SD, String(lastTime));
    debugLogger.print(DebugLogger::SD, " | Voltages: ");
    for (int i = 0; i < batteryCount; ++i)
      debugLogger.print(DebugLogger::SD, String(batteryBuffer[i].volt) + "V; ");
    debugLogger.print(DebugLogger::SD, "| Currents: ");
    for (int i = 0; i < batteryCount; ++i)
      debugLogger.print(DebugLogger::SD, String(batteryBuffer[i].current) + "A; ");
    debugLogger.print(DebugLogger::SD, "| Switches: ");
    for (int i = 0; i < batteryCount; ++i)
      debugLogger.print(DebugLogger::SD, String(batteryBuffer[i].switchState ? "ON" : "OFF") + "; ");
    debugLogger.print(DebugLogger::SD, "| AhCons: ");
    for (int i = 0; i < batteryCount; ++i)
      debugLogger.print(DebugLogger::SD, String(batteryBuffer[i].ampereHourConsumption, 2) + "Ah; ");
    debugLogger.print(DebugLogger::SD, "| AhCharge: ");
    for (int i = 0; i < batteryCount; ++i)
      debugLogger.print(DebugLogger::SD, String(batteryBuffer[i].ampereHourCharge, 2) + "Ah; ");
    debugLogger.print(DebugLogger::SD, "| TotCurrent: " + String(totalCurrent, 1) + "A; ");
    debugLogger.print(DebugLogger::SD, "TotCharge: " + String(totalCharge, 1) + "Ah; ");
    debugLogger.println(DebugLogger::SD, "TotCons: " + String(totalConsumption, 1) + "Ah");
    debugLogger.println(DebugLogger::SD, "");
    // Reset le buffer
    for (int i = 0; i < batteryCount; ++i) batteryBuffer[i].valid = false;
  }
}

// Permet de définir dynamiquement le nombre de batteries
void SDLogger::setBatteryCount(int count) {
  if (count > 0 && count <= MAX_BATTERIES) batteryCount = count;
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