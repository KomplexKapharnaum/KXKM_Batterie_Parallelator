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
 * @file TCAHandler.cpp
 * @brief Implémentation de la classe TCAHandler pour gérer les appareils TCA.
 */

#include "TCAHandler.h"
#include "I2CMutex.h"
#include "INAHandler.h"
#include <KxLogger.h>

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern KxLogger debugLogger;
extern INAHandler inaHandler;

TCAHandler::TCAHandler()
    : TCA_0(TCA_address[0]), 
      TCA_1(TCA_address[1]), 
      TCA_2(TCA_address[2]),
      TCA_3(TCA_address[3]), 
      TCA_4(TCA_address[4]), 
      TCA_5(TCA_address[5]),
      TCA_6(TCA_address[6]), 
      TCA_7(TCA_address[7]),
      Nb_TCA(0) { // Initialisation de Nb_TCA
  memset(TCA_address_connected, 0, sizeof(TCA_address_connected));
}

void TCAHandler::initialize_tca(TCA9535 &tca, const char *name) {
  tca.begin();
  debugLogger.println(KxLogger::INFO, String(name) + " est connecté");
  for (int i = 0; i < 4; i++) {
    tca.pinMode1(i, OUTPUT);
    tca.write1(i, LOW);
  }
  for (int i = 4; i < 8; i++) {
    tca.pinMode1(i, INPUT);
  }
  for (int i = 8; i < 16; i++) {
    tca.pinMode1(i, OUTPUT);
    tca.write1(i, LOW);
  }
}

void TCAHandler::begin() {
  for (int i = 0; i < 8; i++) {
    // CRIT-003: Protect Wire access with I2CLockGuard to prevent race conditions
    I2CLockGuard lock(pdMS_TO_TICKS(100));
    if (!lock.isAcquired()) {
      debugLogger.println(KxLogger::WARNING, "Failed to acquire I2C lock for TCA detection");
      continue;
    }
    
    Wire.beginTransmission(TCA_address[i]);
    delay(1);
    if (Wire.endTransmission() == 0) {
      Nb_TCA++;
      TCA_address_connected[Nb_TCA - 1] = TCA_address[i];

      switch (i) {
      case 0:
        initialize_tca(TCA_0, "TCA_0");
        break;
      case 1:
        initialize_tca(TCA_1, "TCA_1");
        break;
      case 2:
        initialize_tca(TCA_2, "TCA_2");
        break;
      case 3:
        initialize_tca(TCA_3, "TCA_3");
        break;
      case 4:
        initialize_tca(TCA_4, "TCA_4");
        break;
      case 5:
        initialize_tca(TCA_5, "TCA_5");
        break;
      case 6:
        initialize_tca(TCA_6, "TCA_6");
        break;
      case 7:
        initialize_tca(TCA_7, "TCA_7");
        break;
      }
    }
  }
  debugLogger.println(KxLogger::INFO, "trouvé " + String(Nb_TCA) + " appareils");
}

bool TCAHandler::read(int TCA_num, int pin) {
  I2CLockGuard lock;
  if (!lock.isAcquired()) {
    i2cRecordFailure();
    if (i2cShouldRecover()) {
      I2CLockGuard recoveryLock(pdMS_TO_TICKS(20));
      if (recoveryLock.isAcquired()) {
        i2cBusRecovery();
        i2cResetFailureCounter();
        debugLogger.println(KxLogger::WARNING,
                            "I2C recovery executed from TCA.read lock failure");
      }
    }
    return false;
  }
  i2cResetFailureCounter();
  bool val;
  switch (TCA_num) {
  case 0:
    val = TCA_0.read1(pin);
    break;
  case 1:
    val = TCA_1.read1(pin);
    break;
  case 2:
    val = TCA_2.read1(pin);
    break;
  case 3:
    val = TCA_3.read1(pin);
    break;
  case 4:
    val = TCA_4.read1(pin);
    break;
  case 5:
    val = TCA_5.read1(pin);
    break;
  case 6:
    val = TCA_6.read1(pin);
    break;
  case 7:
    val = TCA_7.read1(pin);
    break;
  default:
    return false;
  }
  return val;
}

bool TCAHandler::write(int TCA_num, int pin, bool value) {
  I2CLockGuard lock;
  if (!lock.isAcquired()) {
    i2cRecordFailure();
    if (i2cShouldRecover()) {
      I2CLockGuard recoveryLock(pdMS_TO_TICKS(20));
      if (recoveryLock.isAcquired()) {
        i2cBusRecovery();
        i2cResetFailureCounter();
        debugLogger.println(KxLogger::WARNING,
                            "I2C recovery executed from TCA.write lock failure");
      }
    }
    return false;
  }
  i2cResetFailureCounter();
  switch (TCA_num) {
  case 0:
    TCA_0.write1(pin, value);
    break;
  case 1:
    TCA_1.write1(pin, value);
    break;
  case 2:
    TCA_2.write1(pin, value);
    break;
  case 3:
    TCA_3.write1(pin, value);
    break;
  case 4:
    TCA_4.write1(pin, value);
    break;
  case 5:
    TCA_5.write1(pin, value);
    break;
  case 6:
    TCA_6.write1(pin, value);
    break;
  case 7:
    TCA_7.write1(pin, value);
    break;
  default:
    return false;
  }
  return true;
}

byte TCAHandler::getDeviceAddress(int TCA_num) { return TCA_address[TCA_num]; }

uint8_t TCAHandler::getNbTCA() {
  return Nb_TCA; // Return cached count from begin()
}

void TCAHandler::check_INA_TCA_address() {
  const uint8_t nbIna = inaHandler.getNbINA();
  const uint8_t expectedTca = (nbIna + 3) / 4;
  if (nbIna == 0 || Nb_TCA == 0) {
    debugLogger.println(KxLogger::WARNING,
                        "Topologie INA/TCA invalide: aucun peripherique detecte");
    return;
  }

  if (Nb_TCA != expectedTca) {
    debugLogger.println(
        KxLogger::WARNING,
        "Mismatch topologie INA/TCA: Nb_INA=" + String(nbIna) +
            " Nb_TCA=" + String(Nb_TCA) + " attendu=" + String(expectedTca));
    return;
  }

  debugLogger.println(KxLogger::INFO,
                      "Topologie INA/TCA valide: Nb_INA=" + String(nbIna) +
                          " Nb_TCA=" + String(Nb_TCA));
}
