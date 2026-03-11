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
#include <SD.h>
#include <Arduino.h>
#include <time.h>

extern DebugLogger debugLogger;

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

void SDLogger::begin(const char *baseFilename, CSVConfig config) {
  this->csvConfig = config;
  SPI.begin(18, 5, 19);

  if (!SD.begin(chipSelect)) {
    debugLogger.println(DebugLogger::ERROR, "Échec de l'initialisation de la carte SD !");
    return;
  }

  // Initialiser la base temporelle AVANT de créer le fichier log
  getCurrentTime(baseFilename); // Signature corrigée

  int fileNumber = findNextFileNumber(baseFilename);

  char tempFilename[64];
  snprintf(tempFilename, sizeof(tempFilename), "/%s_%03d.csv", baseFilename, fileNumber);

  if (this->filename != nullptr) free(this->filename);
  this->filename = strdup(tempFilename);

  dataFile = SD.open(this->filename, FILE_WRITE);

  if (dataFile) {
    dataFile.print("Temps");
    for (int i = 0; i < batteryCount; ++i) dataFile.print(csvConfig.separator), dataFile.print("Volt_"), dataFile.print(i+1);
    for (int i = 0; i < batteryCount; ++i) dataFile.print(csvConfig.separator), dataFile.print("Current_"), dataFile.print(i+1);
    for (int i = 0; i < batteryCount; ++i) dataFile.print(csvConfig.separator), dataFile.print("Switch_"), dataFile.print(i+1);
    for (int i = 0; i < batteryCount; ++i) dataFile.print(csvConfig.separator), dataFile.print("AhCons_"), dataFile.print(i+1);
    for (int i = 0; i < batteryCount; ++i) dataFile.print(csvConfig.separator), dataFile.print("AhCharge_"), dataFile.print(i+1);
    dataFile.print(csvConfig.separator); dataFile.print("TotCurrent");
    dataFile.print(csvConfig.separator); dataFile.print("TotCharge");
    dataFile.print(csvConfig.separator); dataFile.print("TotCons");
    dataFile.println();
    dataFile.flush();
    debugLogger.println(DebugLogger::INFO, "Fichier de log créé: " + String(this->filename));
  } else {
    debugLogger.println(DebugLogger::ERROR, "Échec de l'ouverture de " + String(this->filename) + " pour l'écriture !");
  }
}

int SDLogger::findNextFileNumber(const char *baseFilename) {
  int maxNumber = 0;
  File root = SD.open("/");
  if (!root) {
    debugLogger.println(DebugLogger::ERROR, "Impossible d'ouvrir le répertoire racine");
    return 1;
  }
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    String fileName = String(entry.name());
    entry.close();
    String baseFileString = String(baseFilename);
    if (fileName.indexOf(baseFileString + "_") >= 0 && fileName.endsWith(".csv")) {
      int underscorePos = fileName.indexOf(baseFileString + "_") + baseFileString.length() + 1;
      int dotPos = fileName.lastIndexOf(".csv");
      if (dotPos > underscorePos) {
        String numStr = fileName.substring(underscorePos, dotPos);
        int num = numStr.toInt();
        if (num > maxNumber) maxNumber = num;
      }
    }
  }
  root.close();
  debugLogger.println(DebugLogger::INFO, "Prochain numéro de fichier: " + String(maxNumber + 1));
  return maxNumber + 1;
}

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
  strncpy(lastTime, time, sizeof(lastTime)-1);
  lastTime[sizeof(lastTime)-1] = '\0';
  this->totalCurrent = totalCurrent;
  this->totalCharge = totalCharge;
  this->totalConsumption = totalConsumption;
}

void SDLogger::flushLine() {
  for (int i = 0; i < batteryCount; ++i) {
    if (!batteryBuffer[i].valid) {
      debugLogger.println(DebugLogger::ERROR, "flushLine: Données batterie manquantes, ligne non écrite.");
      return;
    }
  }
  dataFile = SD.open(filename, FILE_APPEND);
  if (dataFile) {
    dataFile.print(lastTime);
    for (int i = 0; i < batteryCount; ++i) dataFile.print(csvConfig.separator), dataFile.print(batteryBuffer[i].volt);
    for (int i = 0; i < batteryCount; ++i) dataFile.print(csvConfig.separator), dataFile.print(batteryBuffer[i].current);
    for (int i = 0; i < batteryCount; ++i) dataFile.print(csvConfig.separator), dataFile.print(batteryBuffer[i].switchState ? "ON" : "OFF");
    for (int i = 0; i < batteryCount; ++i) dataFile.print(csvConfig.separator), dataFile.print(batteryBuffer[i].ampereHourConsumption, 4);
    for (int i = 0; i < batteryCount; ++i) dataFile.print(csvConfig.separator), dataFile.print(batteryBuffer[i].ampereHourCharge, 4);
    dataFile.print(csvConfig.separator); dataFile.print(totalCurrent, 1);
    dataFile.print(csvConfig.separator); dataFile.print(totalCharge, 1);
    dataFile.print(csvConfig.separator); dataFile.print(totalConsumption, 1);
    dataFile.println();
    dataFile.flush();
    dataFile.close();

    debugLogger.print(DebugLogger::SD, "Données ligne CSV: ");
    debugLogger.print(DebugLogger::SD, String(lastTime));
    debugLogger.print(DebugLogger::SD, " | Voltages: ");
    for (int i = 0; i < batteryCount; ++i) debugLogger.print(DebugLogger::SD, String(batteryBuffer[i].volt) + "V; ");
    debugLogger.print(DebugLogger::SD, "| Currents: ");
    for (int i = 0; i < batteryCount; ++i) debugLogger.print(DebugLogger::SD, String(batteryBuffer[i].current) + "A; ");
    debugLogger.print(DebugLogger::SD, "| Switches: ");
    for (int i = 0; i < batteryCount; ++i) debugLogger.print(DebugLogger::SD, String(batteryBuffer[i].switchState ? "ON" : "OFF") + "; ");
    debugLogger.print(DebugLogger::SD, "| AhCons: ");
    for (int i = 0; i < batteryCount; ++i) debugLogger.print(DebugLogger::SD, String(batteryBuffer[i].ampereHourConsumption, 2) + "Ah; ");
    debugLogger.print(DebugLogger::SD, "| AhCharge: ");
    for (int i = 0; i < batteryCount; ++i) debugLogger.print(DebugLogger::SD, String(batteryBuffer[i].ampereHourCharge, 2) + "Ah; ");
    debugLogger.print(DebugLogger::SD, "| TotCurrent: " + String(totalCurrent, 1) + "A; ");
    debugLogger.print(DebugLogger::SD, "TotCharge: " + String(totalCharge, 1) + "Ah; ");
    debugLogger.println(DebugLogger::SD, "TotCons: " + String(totalConsumption, 1) + "Ah");
    debugLogger.println(DebugLogger::SD, "");
    for (int i = 0; i < batteryCount; ++i) batteryBuffer[i].valid = false;
  } else {
    debugLogger.println(DebugLogger::ERROR, "flushLine: Impossible d'ouvrir le fichier pour écriture !");
  }
}

void SDLogger::setBatteryCount(int count) {
  if (count > 0 && count <= MAX_BATTERIES) batteryCount = count;
}

bool SDLogger::shouldLog() {
  unsigned long currentTime = millis();
  if (currentTime - lastLogTime >= (unsigned long)log_at_time) {
    lastLogTime = currentTime;
    return true;
  }
  return false;
}

void SDLogger::setLogTime(int time) {
  log_at_time = time * 1000;
}

time_t SDLogger::getLastLogTimeFromSD(const char* logFileBase) {
  File root = SD.open("/");
  if (!root) {
    debugLogger.println(DebugLogger::SD, "Impossible d'ouvrir le répertoire racine SD !");
    return 0;
  }
  String lastFileName = "";
  int maxNum = 0;
  debugLogger.println(DebugLogger::SD, "Recherche du dernier fichier log sur la SD...");
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    String name = String(entry.name());
    if (name.startsWith("/")) name = name.substring(1);
    if (name.startsWith(logFileBase) && name.endsWith(".csv")) {
      int underscore = name.lastIndexOf('_');
      int dot = name.lastIndexOf('.');
      if (underscore > 0 && dot > underscore) {
        int num = name.substring(underscore + 1, dot).toInt();
        debugLogger.print(DebugLogger::SD, "Fichier trouvé : ");
        debugLogger.print(DebugLogger::SD, name);
        debugLogger.print(DebugLogger::SD, " numéro : ");
        debugLogger.println(DebugLogger::SD, String(num));
        if (num > maxNum) {
          maxNum = num;
          lastFileName = "/" + name;
        }
      }
    }
    entry.close();
  }
  root.close();
  if (lastFileName == "") {
    debugLogger.println(DebugLogger::SD, "Aucun fichier log trouvé sur la SD.");
    return 0;
  }
  debugLogger.print(DebugLogger::SD, "Dernier fichier log détecté : ");
  debugLogger.println(DebugLogger::SD, lastFileName);

  File lastFile = SD.open(lastFileName.c_str(), FILE_READ);
  if (!lastFile) {
    debugLogger.println(DebugLogger::SD, "Impossible d'ouvrir le dernier fichier log !");
    return 0;
  }
  String lastLine;
  while (lastFile.available()) {
    lastLine = lastFile.readStringUntil('\n');
  }
  lastFile.close();
  debugLogger.print(DebugLogger::SD, "Dernière ligne lue : ");
  debugLogger.println(DebugLogger::SD, lastLine);

  int sep = lastLine.indexOf(';');
  String dateStr = (sep > 0) ? lastLine.substring(0, sep) : lastLine;
  debugLogger.print(DebugLogger::SD, "Extraction de la date : ");
  debugLogger.println(DebugLogger::SD, dateStr);

  struct tm timeinfo;
  memset(&timeinfo, 0, sizeof(struct tm));
  if (sscanf(dateStr.c_str(), "%d-%d-%d %d:%d:%d",
             &timeinfo.tm_year, &timeinfo.tm_mon, &timeinfo.tm_mday,
             &timeinfo.tm_hour, &timeinfo.tm_min, &timeinfo.tm_sec) == 6) {
    timeinfo.tm_year -= 1900;
    timeinfo.tm_mon -= 1;
    debugLogger.println(DebugLogger::SD, "Format date log reconnu (YYYY-MM-DD HH:MM:SS)");
    return mktime(&timeinfo);
  }
  char month[4];
  int day, year, hour, minute, second;
  if (sscanf(dateStr.c_str(), "%3s %d %d %d:%d:%d", month, &day, &year, &hour, &minute, &second) == 6) {
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int monthNum = 0;
    for (int i = 0; i < 12; i++) {
      if (strcmp(month, months[i]) == 0) {
        monthNum = i;
        break;
      }
    }
    memset(&timeinfo, 0, sizeof(struct tm));
    timeinfo.tm_year = year - 1900;
    timeinfo.tm_mon = monthNum;
    timeinfo.tm_mday = day;
    timeinfo.tm_hour = hour;
    timeinfo.tm_min = minute;
    timeinfo.tm_sec = second;
    debugLogger.println(DebugLogger::SD, "Format date compilation reconnu (MMM DD YYYY HH:MM:SS)");
    return mktime(&timeinfo);
  }
  debugLogger.println(DebugLogger::SD, "Aucun format de date reconnu dans la dernière ligne.");
  return 0;
}

String SDLogger::getCurrentTime(const char* logFileBase) {
  static time_t baseTime = 0;
  static bool initialized = false;

  if (!initialized) {
    time_t lastLogTime = getLastLogTimeFromSD(logFileBase);
    if (lastLogTime > 0) {
      baseTime = lastLogTime;
      debugLogger.print(DebugLogger::SD, "Base temporelle initialisée à la dernière date du log SD : ");
      debugLogger.println(DebugLogger::SD, String(ctime(&baseTime)));
    } else {
      const char *compileDate = __DATE__;
      const char *compileTime = __TIME__;
      char month[4];
      int day, year, hour, minute, second;
      sscanf(compileDate, "%3s %d %d", month, &day, &year);
      sscanf(compileTime, "%d:%d:%d", &hour, &minute, &second);
      const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
      int monthNum = 0;
      for (int i = 0; i < 12; i++) {
        if (strcmp(month, months[i]) == 0) {
          monthNum = i;
          break;
        }
      }
      struct tm timeinfo;
      memset(&timeinfo, 0, sizeof(struct tm));
      timeinfo.tm_year = year - 1900;
      timeinfo.tm_mon = monthNum;
      timeinfo.tm_mday = day;
      timeinfo.tm_hour = hour;
      timeinfo.tm_min = minute;
      timeinfo.tm_sec = second;
      baseTime = mktime(&timeinfo);
      debugLogger.print(DebugLogger::SD, "Base temporelle initialisée à la date de compilation : ");
      debugLogger.println(DebugLogger::SD, String(ctime(&baseTime)));
    }
    initialized = true;
  }

  time_t now = baseTime + (millis() / 1000);
  struct tm *timeinfo = localtime(&now);
  char buffer[20];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
  return String(buffer);
}
