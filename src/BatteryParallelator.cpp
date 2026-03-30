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
 * @file BatteryParallelator.cpp
 * @brief Implémentation de la classe BATTParallelator pour gérer les batteries
 * en parallèle.
 *
 * Ce fichier contient les définitions des méthodes de la classe
 * BATTParallelator, qui permet de gérer les batteries en parallèle, y compris
 * la vérification de l'état, la commutation et la gestion des courants et
 * tensions.
 */

#include "BatteryParallelator.h"
#include <KxLogger.h>
#include <cmath>

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern KxLogger debugLogger;
extern BatteryManager batteryManager; // Ajouter cette ligne

// Définition des états des batteries
enum BatteryState {
  CONNECTED,
  DISCONNECTED,
  RECONNECTING,
  ERROR // Nouveau mode pour les erreurs critiques
};

/**
 * @brief Constructeur de la classe BATTParallelator.
 */
BATTParallelator::BATTParallelator()
  : Nb_switch_max(5), max_discharge_current(1000)
{
  stateMutex = xSemaphoreCreateMutex();
  configASSERT(stateMutex != NULL);

  memset(battery_voltages, 0, sizeof(battery_voltages));
  memset(Nb_switch, 0, sizeof(Nb_switch));
  memset(reconnect_time, 0, sizeof(reconnect_time));
}

bool BATTParallelator::lockState(TickType_t timeout) {
  if (stateMutex == NULL) {
    return false;
  }
  return xSemaphoreTake(stateMutex, timeout) == pdTRUE;
}

void BATTParallelator::set_battery_voltage(int INA_num, float voltage) {
  if (INA_num < 0 || INA_num >= 16) {
    return;
  }

  if (lockState()) {
    battery_voltages[INA_num] = voltage;
    xSemaphoreGive(stateMutex);
  }
}

void BATTParallelator::copy_battery_voltages(float *dest, int count) {
  if (dest == nullptr || count <= 0) {
    return;
  }

  const int maxCopy = (count > 16) ? 16 : count;
  if (lockState()) {
    for (int i = 0; i < maxCopy; ++i) {
      dest[i] = battery_voltages[i];
    }
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir le nombre maximal de commutateurs.
 * @param nb Nombre maximal de commutateurs.
 */
void BATTParallelator::set_nb_switch_on(int nb) { Nb_switch_max = nb; }

/**
 * @brief Définir le délai de reconnexion.
 * @param delay Délai de reconnexion en millisecondes.
 */
void BATTParallelator::set_reconnect_delay(int delay) {
  if (lockState()) {
    reconnect_delay = delay;
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir la tension maximale.
 * @param voltage Tension maximale en volts.
 */
void BATTParallelator::set_max_voltage(float voltage) {
  if (lockState()) {
    mem_set_max_voltage = voltage;
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir la tension minimale.
 * @param voltage Tension minimale en volts.
 */
void BATTParallelator::set_min_voltage(float voltage) {
  if (lockState()) {
    mem_set_min_voltage = voltage;
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir la différence de tension maximale.
 * @param diff Différence de tension maximale en volts.
 */
void BATTParallelator::set_max_diff_voltage(float diff) {
  if (lockState()) {
    mem_set_voltage_diff = diff;
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir la différence de courant maximale.
 * @param diff Différence de courant maximale en ampères.
 */
void BATTParallelator::set_max_diff_current(float diff) {
  if (lockState()) {
    mem_set_current_diff = diff;
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir le courant maximal.
 * @param current Courant maximal en ampères.
 */
void BATTParallelator::set_max_current(float current) {
  if (lockState()) {
    mem_set_max_current = current;
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir le courant de décharge maximal.
 * @param current Courant de décharge maximal en ampères.
 */
void BATTParallelator::set_max_discharge_current(float current) {
  if (lockState()) {
    mem_set_max_discharge_current = current;
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Définir le courant de charge maximal.
 * @param current Courant de charge maximal en ampères.
 */
void BATTParallelator::set_max_charge_current(float current) {
  if (lockState()) {
    mem_set_max_charge_current = current;
    xSemaphoreGive(stateMutex);
  }
}

bool BATTParallelator::isValidBatteryIndex(int INA_num) const {
  return INA_num >= 0 && INA_num < inaHandler.getNbINA();
}

/**
 * @brief Obtenir le numéro du TCA à partir du numéro de l'INA.
 * @param INA_num Numéro de l'INA.
 * @return Numéro du TCA.
 */
uint8_t BATTParallelator::TCA_num(int INA_num) {
  if (!isValidBatteryIndex(INA_num)) {
    return 0xFF;
  }

  int address = inaHandler.getDeviceAddress(INA_num);
  if (address >= 64 && address <= 79) {
    return (address - 64) / 4;
  } else {
    return 0xFF;
  }
}

/**
 * @brief Éteindre une batterie.
 * @param batt_number Numéro de la batterie.
 */
bool BATTParallelator::switch_off_battery(int INA_num) {
  if (!isValidBatteryIndex(INA_num)) {
    debugLogger.println(KxLogger::WARNING,
                        "Invalid battery index for switch_off_battery: " +
                            String(INA_num));
    return false;
  }

  const uint8_t tcaNum = BATTParallelator::TCA_num(INA_num);
  if (tcaNum == 0xFF) {
    debugLogger.println(KxLogger::WARNING,
                        "Invalid TCA mapping for battery " +
                            String(INA_num + 1));
    return false;
  }

  const int OUT_num = (inaHandler.getDeviceAddress(INA_num) - 64) % 4;
  debugLogger.println(debugLogger.BATTERY, "Switching off battery " +
                                               String(INA_num + 1) +
                                               " on TCA " + String(tcaNum) +
                                               " OUT " + String(OUT_num));
  const bool switchWriteOk = tcaHandler.write(tcaNum, OUT_num, 0);
  const bool redLedOk = tcaHandler.write(tcaNum, OUT_num * 2 + 8, 1);
  const bool greenLedOk = tcaHandler.write(tcaNum, OUT_num * 2 + 9, 0);
  vTaskDelay(pdMS_TO_TICKS(50)); // wait for the battery to switch off
  return switchWriteOk && redLedOk && greenLedOk;
}

/**
 * @brief Allumer une batterie.
 * @param batt_number Numéro de la batterie.
 */
bool BATTParallelator::switch_on_battery(int INA_num) {
  if (!isValidBatteryIndex(INA_num)) {
    debugLogger.println(KxLogger::WARNING,
                        "Invalid battery index for switch_on_battery: " +
                            String(INA_num));
    return false;
  }

  const uint8_t tcaNum = BATTParallelator::TCA_num(INA_num);
  if (tcaNum == 0xFF) {
    debugLogger.println(KxLogger::WARNING,
                        "Invalid TCA mapping for battery " +
                            String(INA_num + 1));
    return false;
  }

  const int OUT_num = (inaHandler.getDeviceAddress(INA_num) - 64) % 4;

  debugLogger.println(KxLogger::BATTERY, "Switching on battery " +
                                                String(INA_num + 1) +
                                                " on TCA " + String(tcaNum));
  const bool switchWriteOk = tcaHandler.write(tcaNum, OUT_num, 1);
  const bool redLedOk = tcaHandler.write(tcaNum, OUT_num * 2 + 8, 0);
  const bool greenLedOk = tcaHandler.write(tcaNum, OUT_num * 2 + 9, 1);
  vTaskDelay(pdMS_TO_TICKS(100)); // MOSFET dead-time protection
  return switchWriteOk && redLedOk && greenLedOk;
}

/**
 * @brief Vérifier l'offset de tension de la batterie.
 * @param INA_num Numéro de l'INA.
 * @param offset Offset de tension en volts.
 * @return true si l'offset est dans la plage acceptable, false sinon.
 */
bool BATTParallelator::check_voltage_offset(int INA_num, float offset) {
  float voltage = inaHandler.read_volt(INA_num);
  if (std::isnan(voltage)) return false; // I2C error — treat as out of range
  float averageVoltage = batteryManager.getAverageVoltage();

  if (fabs(voltage - averageVoltage) > offset) {
    if (voltage > 1) {
      //   debugLogger.println(KxLogger::BATTERY, "Battery " + String(INA_num
      //   + 1) +
      //                                      " voltage is out of offset range:
      //                                      " + String(voltage));
    }
    return false;
  }
  if (voltage > 1) {
    //  debugLogger.println(KxLogger::BATTERY, "Battery " + String(INA_num +
    //  1) +
    //                                     " voltage is in offset range ! :D " +
    //                                     String(voltage));
  }
  return true;
}

/**
 * @brief Vérifier l'état de charge de la batterie.
 * @param INA_num Numéro de l'INA.
 * @return true si la batterie est en charge (courant négatif), false sinon.
 */
bool BATTParallelator::check_charge_status(int INA_num) {
  float current = inaHandler.read_current(INA_num);
  if (std::isnan(current)) return false; // I2C error — assume not charging
  return current < 0;
}

/**
 * @brief Commuter une batterie.
 * @param INA_num Numéro de l'INA.
 * @param switch_on true pour allumer la batterie, false pour l'éteindre.
 * @return true si la batterie est allumée, false sinon.
 */
bool BATTParallelator::switch_battery(int INA_num, bool switch_on) {
  if (!isValidBatteryIndex(INA_num)) {
    debugLogger.println(KxLogger::WARNING,
                        "switch_battery called with invalid index: " +
                            String(INA_num));
    return false;
  }

  const uint8_t TCA_number = TCA_num(INA_num);
  if (TCA_number == 0xFF) {
    debugLogger.println(KxLogger::WARNING,
                        "switch_battery failed: invalid TCA mapping for battery " +
                            String(INA_num + 1));
    return false;
  }

  const int OUT_number = (inaHandler.getDeviceAddress(INA_num) - 64) % 4;

  debugLogger.print(KxLogger::BATTERY, "Switching ");
  if (switch_on)
    debugLogger.print(KxLogger::BATTERY, "on ");
  else
    debugLogger.print(KxLogger::BATTERY, "off ");
  debugLogger.print(KxLogger::BATTERY, " battery " + String(INA_num + 1) +
                                              " on TCA " + String(TCA_number) +
                                              " OUT " + String(OUT_number));

  if (switch_on) {
    return switch_on_battery(INA_num);
  }

  return switch_off_battery(INA_num);
}

/**
 * @brief Vérifier et gérer l'état de connexion de la batterie.
 * @param INA_num Numéro de l'INA.
 */
void BATTParallelator::check_battery_connected_status(int INA_num) {
  if (!isValidBatteryIndex(INA_num)) {
    debugLogger.println(KxLogger::WARNING,
                        "check_battery_connected_status: index invalide " +
                            String(INA_num));
    return;
  }

  const float voltageOffset = 0.5; // Offset de tension en volts
  float voltage = inaHandler.read_volt(INA_num);
  float current = inaHandler.read_current(INA_num);

  // I2C error — don't make protection decisions on garbage data
  if (std::isnan(voltage) || std::isnan(current)) {
    debugLogger.println(KxLogger::WARNING,
        "I2C read error on battery " + String(INA_num + 1) +
        " — skipping protection check");
    return;
  }

  BatteryState state;
  int localNbSwitch = 0;
  long localReconnectTime = 0;

  if (lockState()) {
    localNbSwitch = Nb_switch[INA_num];
    localReconnectTime = reconnect_time[INA_num];
    xSemaphoreGive(stateMutex);
  }

  // Vérifier l'état de la batterie
  // mem_set_max_current is in mA, current is in A — convert for comparison
  const float overcurrent_limit_A = (2.0f * mem_set_max_current) / 1000.0f;
  if (voltage < 0 ||
      current > overcurrent_limit_A) { // Conditions critiques
    state = ERROR;
  } else if (voltage < 1) {
    state = DISCONNECTED;
  } else if (!check_battery_status(INA_num) ||
             !check_voltage_offset(INA_num, voltageOffset)) {
    state = DISCONNECTED;
  } else if (localNbSwitch == 0 || // Première connexion
             (localNbSwitch < Nb_switch_max &&
              (millis() - localReconnectTime > reconnect_delay))) {
    state = RECONNECTING;
  } else {
    state = CONNECTED;
  }

  // Gérer l'état de la batterie
  switch (state) {
  case CONNECTED:
    debugLogger.println(KxLogger::BATTERY,
                        "Battery " + String(INA_num + 1) + " is connected.");
    break;

  case RECONNECTING:
    if (is_voltage_within_range(voltage) && is_current_within_range(current)) {
      debugLogger.println(KxLogger::BATTERY,
                          "Connecting/Reconnecting battery " +
                              String(INA_num + 1));
      switch_battery(INA_num, 1); // Allumer la batterie
      debugLogger.println(KxLogger::BATTERY, "Battery " +
                                                    String(INA_num + 1) +
                                                    " is now connected.");
      if (lockState()) {
        Nb_switch[INA_num]++;
        reconnect_time[INA_num] = millis();
        xSemaphoreGive(stateMutex);
      }
    } else {
      debugLogger.println(KxLogger::BATTERY,
                          "Battery " + String(INA_num + 1) +
                              " does not meet connection criteria.");
    }
    break;

  case DISCONNECTED:
    debugLogger.println(KxLogger::BATTERY,
                        "Battery " + String(INA_num + 1) + " is disconnected.");
    switch_battery(INA_num, 0); // Eteindre la batterie
    break;

  case ERROR:
    debugLogger.println(KxLogger::BATTERY,
                        "Critical error on battery " + String(INA_num + 1) +
                            ". Immediate action required!");
    switch_battery(INA_num, 0); //  Eteindre la batterie
    switch_off_battery(INA_num);
    // Ajouter des actions spécifiques pour le mode erreur si nécessaire
    break;
  }
}

/**
 * @brief Comparer la tension de la batterie avec une tension maximale.
 * @param voltage Tension de la batterie.
 * @param voltage_max Tension maximale.
 * @param diff Différence de tension acceptable.
 * @return true si la tension est dans la plage acceptable, false sinon.
 */
bool BATTParallelator::compare_voltage(float voltage, float voltage_max,
                                       float diff) {
  if (voltage < voltage_max - diff || voltage > voltage_max + diff) {
    debugLogger.println(KxLogger::BATTERY,
                        "Voltage out of range: " + String(voltage));
    debugLogger.println(KxLogger::BATTERY,
                        "Max voltage: " + String(voltage_max));
    return false; // Si la tension est invalide, retourner false
  }
  return true;
}

/**
 * @brief Trouver la tension maximale parmi les batteries.
 * @param battery_voltages Tableau des tensions des batteries.
 * @param num_batteries Nombre de batteries.
 * @return Tension maximale.
 */
float BATTParallelator::find_max_voltage(float *battery_voltages, int Nb_Batt) {
  if (Nb_Batt <= 0) return 0.0f;
  float maxVoltage = battery_voltages[0];
  for (int i = 1; i < Nb_Batt; i++) {
    if (battery_voltages[i] > maxVoltage) {
      maxVoltage = battery_voltages[i];
    }
  }
  return maxVoltage;
}

/**
 * @brief Trouver la tension minimale parmi les batteries.
 * @param battery_voltages Tableau des tensions des batteries.
 * @param num_batteries Nombre de batteries.
 * @return Tension minimale.
 */
float BATTParallelator::find_min_voltage(float *battery_voltages,
                                         int num_batteries) {
  if (num_batteries <= 0) return 0.0f;
  float min_voltage = battery_voltages[0];
  for (int i = 1; i < num_batteries; i++) {
    if (battery_voltages[i] < min_voltage && battery_voltages[i] > 1) {
      min_voltage = battery_voltages[i];
    }
  }
  return min_voltage;
}

/**
 * @brief Réinitialiser le compteur de commutateurs pour une batterie.
 * @param INA_num Numéro de l'INA.
 */
void BATTParallelator::reset_switch_count(int INA_num) {
  if (INA_num < 0 || INA_num >= 16) {
    return;
  }
  if (lockState()) {
    Nb_switch[INA_num] = 0;
    reconnect_time[INA_num] = 0;
    xSemaphoreGive(stateMutex);
  }
}

/**
 * @brief Détecter le nombre de batteries dont la tension est supérieure à 5V.
 * @return Nombre de batteries détectées.
 */
int BATTParallelator::detect_batteries() {
  int count = 0;
  for (int i = 0; i < inaHandler.getNbINA(); i++) {
    float v = inaHandler.read_volt(i);
    if (!std::isnan(v) && v > 5.0) {
      count++;
    }
  }
  return count;
}

/**
 * @brief Vérifie si la tension est dans les limites acceptables.
 * @param voltage Tension de la batterie.
 * @return true si la tension est dans les limites, false sinon.
 */
bool BATTParallelator::is_voltage_within_range(float voltage) {
  voltage = voltage * 1000; // Convertir en mV
  // Vérifier si la tension est dans les limites définies
  if (voltage < mem_set_min_voltage || voltage > mem_set_max_voltage) {
    debugLogger.println(KxLogger::BATTERY, "voltage : " + String(voltage));
    debugLogger.println(KxLogger::BATTERY,
                        "Min voltage : " + String(mem_set_min_voltage));
    debugLogger.println(KxLogger::BATTERY,
                        "Max voltage : " + String(mem_set_max_voltage));
    debugLogger.println(KxLogger::BATTERY,
                        "Voltage out of range: " + String(voltage));
    return false;
  }
  return true;
}

/**
 * @brief Vérifie si le courant est dans les limites acceptables.
 * @param current Courant de la batterie.
 * @return true si le courant est dans les limites, false sinon.
 */
bool BATTParallelator::is_current_within_range(float current) {
  // mem_set_max_current/charge are in mA, current is in A — convert
  if (abs(current) > mem_set_max_current / 1000.0f ||
      current < -(mem_set_max_charge_current / 1000.0f)) {
    debugLogger.println(KxLogger::BATTERY,
                        "Current out of range: " + String(current));
    return false;
  }
  return true;
}

/**
 * @brief Vérifie si les différences de tension et de courant sont acceptables.
 * @param voltage Tension de la batterie.
 * @param current Courant de la batterie.
 * @return true si les différences sont acceptables, false sinon.
 */
bool BATTParallelator::is_difference_acceptable(float voltage, float current) {
  // mem_set_current_diff is in mA, current is in A — convert
  return compare_voltage(voltage, mem_set_max_voltage, mem_set_voltage_diff) &&
         fabs(current) <= mem_set_current_diff / 1000.0f;
}

/**
 * @brief Vérifier l'état de la batterie.
 * @param INA_num Numéro de l'INA.
 * @return true si la batterie est en bon état, false sinon.
 */
bool BATTParallelator::check_battery_status(int INA_num) {
  float voltage = inaHandler.read_volt(INA_num);
  float current = inaHandler.read_current(INA_num);

  // I2C error — treat as bad status to prevent connecting on garbage
  if (std::isnan(voltage) || std::isnan(current)) {
    debugLogger.println(KxLogger::WARNING,
        "I2C read error in check_battery_status() for battery " +
        String(INA_num + 1));
    return false;
  }

  // Vérifier si la tension est valide
  if (voltage <= 1) {
    debugLogger.println(KxLogger::BATTERY, "Battery " + String(INA_num + 1) +
                                                  " voltage is invalid.");
    return false;
  }

  // Vérifier les seuils de tension, de courant et les différences
  if (is_voltage_within_range(voltage) && is_current_within_range(current) &&
      is_difference_acceptable(voltage, current)) {
    debugLogger.println(KxLogger::BATTERY, "Battery " + String(INA_num + 1) +
                                                  " is in good condition.");
    return true;
  } else {
    debugLogger.println(KxLogger::BATTERY, "Battery " + String(INA_num + 1) +
                                                  " is not in good condition.");
    return false;
  }
}