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
#include <DebugLogger.h>

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern DebugLogger debugLogger;
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
    : Nb_switch_max(5), max_discharge_current(1000),
      max_charge_current(1000) // Initialiser les courants max
{
  memset(Nb_switch, 0, sizeof(Nb_switch));
  memset(reconnect_time, 0, sizeof(reconnect_time));
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
  reconnect_delay = delay;
}

/**
 * @brief Définir la tension maximale.
 * @param voltage Tension maximale en volts.
 */
void BATTParallelator::set_max_voltage(float voltage) {
  mem_set_max_voltage = voltage;
}

/**
 * @brief Définir la tension minimale.
 * @param voltage Tension minimale en volts.
 */
void BATTParallelator::set_min_voltage(float voltage) {
  mem_set_min_voltage = voltage;
}

/**
 * @brief Définir la différence de tension maximale.
 * @param diff Différence de tension maximale en volts.
 */
void BATTParallelator::set_max_diff_voltage(float diff) {
  mem_set_voltage_diff = diff;
}

/**
 * @brief Définir la différence de courant maximale.
 * @param diff Différence de courant maximale en ampères.
 */
void BATTParallelator::set_max_diff_current(float diff) {
  mem_set_current_diff = diff;
}

/**
 * @brief Définir le courant maximal.
 * @param current Courant maximal en ampères.
 */
void BATTParallelator::set_max_current(float current) {
  mem_set_max_current = current;
}

/**
 * @brief Définir le courant de décharge maximal.
 * @param current Courant de décharge maximal en ampères.
 */
void BATTParallelator::set_max_discharge_current(float current) {
  mem_set_max_discharge_current = current;
}

/**
 * @brief Définir le courant de charge maximal.
 * @param current Courant de charge maximal en ampères.
 */
void BATTParallelator::set_max_charge_current(float current) {
  mem_set_max_charge_current = current;
}

/**
 * @brief Obtenir le numéro du TCA à partir du numéro de l'INA.
 * @param INA_num Numéro de l'INA.
 * @return Numéro du TCA.
 */
uint8_t BATTParallelator::TCA_num(int INA_num) {
  int address = inaHandler.getDeviceAddress(INA_num);
  if (address >= 64 && address <= 79) {
    return (address - 64) / 4;
  } else {
    return 10; // return 10 if the address is not in the range
  }
}

/**
 * @brief Éteindre une batterie.
 * @param batt_number Numéro de la batterie.
 */
void BATTParallelator::switch_off_battery(int INA_num) {
  int TCA_num = BATTParallelator::TCA_num(INA_num);
  int OUT_num = (inaHandler.getDeviceAddress(INA_num) - 64) % 4;
  debugLogger.println(debugLogger.BATTERY, "Switching off battery " +
                                               String(INA_num + 1) +
                                               " on TCA " + String(TCA_num) +
                                               " OUT " + String(OUT_num));
  tcaHandler.write(TCA_num, OUT_num, 0);     // switch off the battery
  tcaHandler.write(TCA_num, OUT_num * 2 + 8, 1); // set red led on
  tcaHandler.write(TCA_num, OUT_num * 2 + 9, 0); // set green led off
  vTaskDelay(pdMS_TO_TICKS(50)); // wait for the battery to switch off
}

/**
 * @brief Allumer une batterie.
 * @param batt_number Numéro de la batterie.
 */
void BATTParallelator::switch_on_battery(int INA_num) {
  int TCA_num = BATTParallelator::TCA_num(INA_num);
  int OUT_num = (inaHandler.getDeviceAddress(INA_num) - 64) % 4;

  debugLogger.println(DebugLogger::BATTERY, "Switching on battery " +
                                                String(INA_num + 1) +
                                                " on TCA " + String(TCA_num));
  tcaHandler.write(TCA_num, OUT_num, 1);         // switch on the battery
  tcaHandler.write(TCA_num, OUT_num * 2 + 8, 0); // set red led off
  tcaHandler.write(TCA_num, OUT_num * 2 + 9, 1); // set green led on
}

/**
 * @brief Vérifier l'offset de tension de la batterie.
 * @param INA_num Numéro de l'INA.
 * @param offset Offset de tension en volts.
 * @return true si l'offset est dans la plage acceptable, false sinon.
 */
bool BATTParallelator::check_voltage_offset(int INA_num, float offset) {
  float voltage = inaHandler.read_volt(INA_num);
  float averageVoltage = batteryManager.getAverageVoltage();

  if (abs(voltage - averageVoltage) > offset) {
    if (voltage > 1) {
      //   debugLogger.println(DebugLogger::BATTERY, "Battery " + String(INA_num
      //   + 1) +
      //                                      " voltage is out of offset range:
      //                                      " + String(voltage));
    }
    return false;
  }
  if (voltage > 1) {
    //  debugLogger.println(DebugLogger::BATTERY, "Battery " + String(INA_num +
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
  return inaHandler.read_current(INA_num) < 0;
}

/**
 * @brief Commuter une batterie.
 * @param INA_num Numéro de l'INA.
 * @param switch_on true pour allumer la batterie, false pour l'éteindre.
 * @return true si la batterie est allumée, false sinon.
 */
bool BATTParallelator::switch_battery(int INA_num, bool switch_on) {
  int TCA_number = TCA_num(INA_num);
  int OUT_number = (inaHandler.getDeviceAddress(INA_num) - 64) % 4;

  debugLogger.print(DebugLogger::BATTERY, "Switching ");
  if (switch_on)
    debugLogger.print(DebugLogger::BATTERY, "on ");
  else
    debugLogger.print(DebugLogger::BATTERY, "off ");
  debugLogger.print(DebugLogger::BATTERY, " battery " + String(INA_num + 1) +
                                              " on TCA " + String(TCA_number) +
                                              " OUT " + String(OUT_number));

  if (switch_on) {
    switch_on_battery(INA_num);
    return true;
  } else {
    switch_off_battery(INA_num);
    return false;
  }
}

/**
 * @brief Vérifier et gérer l'état de connexion de la batterie.
 * @param INA_num Numéro de l'INA.
 */
void BATTParallelator::check_battery_connected_status(int INA_num) {
  const float voltageOffset = 0.5; // Offset de tension en volts
  float voltage = inaHandler.read_volt(INA_num);
  float current = inaHandler.read_current(INA_num);

  BatteryState state;

  // Vérifier l'état de la batterie
  if (voltage < 0 ||
      current > 2 * mem_set_max_current) { // Conditions critiques
    state = ERROR;
  } else if (voltage < 1) {
    state = DISCONNECTED;
  } else if (!check_battery_status(INA_num) ||
             !check_voltage_offset(INA_num, voltageOffset)) {
    state = DISCONNECTED;
  } else if (Nb_switch[INA_num] == 0 || // Première connexion
             (Nb_switch[INA_num] < Nb_switch_max &&
              (millis() - reconnect_time[INA_num] > reconnect_delay))) {
    state = RECONNECTING;
  } else {
    state = CONNECTED;
  }

  // Gérer l'état de la batterie
  switch (state) {
  case CONNECTED:
    debugLogger.println(DebugLogger::BATTERY,
                        "Battery " + String(INA_num + 1) + " is connected.");
    break;

  case RECONNECTING:
    if (is_voltage_within_range(voltage) && is_current_within_range(current)) {
      debugLogger.println(DebugLogger::BATTERY,
                          "Connecting/Reconnecting battery " +
                              String(INA_num + 1));
      switch_battery(INA_num, 1); // Allumer la batterie
      debugLogger.println(DebugLogger::BATTERY, "Battery " +
                                                    String(INA_num + 1) +
                                                    " is now connected.");
      Nb_switch[INA_num]++;
      reconnect_time[INA_num] = millis();
    } else {
      debugLogger.println(DebugLogger::BATTERY,
                          "Battery " + String(INA_num + 1) +
                              " does not meet connection criteria.");
    }
    break;

  case DISCONNECTED:
    debugLogger.println(DebugLogger::BATTERY,
                        "Battery " + String(INA_num + 1) + " is disconnected.");
    switch_battery(INA_num, 0); // Eteindre la batterie
    break;

  case ERROR:
    debugLogger.println(DebugLogger::BATTERY,
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
  if (voltage < max_voltage - diff || voltage > max_voltage + diff) {
    debugLogger.println(DebugLogger::BATTERY,
                        "Voltage out of range: " + String(voltage));
    debugLogger.println(DebugLogger::BATTERY,
                        "Max voltage: " + String(max_voltage));
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
  max_voltage = battery_voltages[0];
  for (int i = 1; i < Nb_Batt; i++) {
    if (battery_voltages[i] > max_voltage) {
      max_voltage = battery_voltages[i];
    }
  }
  return max_voltage;
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
  Nb_switch[INA_num] = 0;
  reconnect_time[INA_num] = 0;
}

/**
 * @brief Détecter le nombre de batteries dont la tension est supérieure à 5V.
 * @return Nombre de batteries détectées.
 */
int BATTParallelator::detect_batteries() {
  int count = 0;
  for (int i = 0; i < inaHandler.getNbINA(); i++) {
    if (inaHandler.read_volt(i) > 5.0) {
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
    debugLogger.println(DebugLogger::BATTERY, "voltage : " + String(voltage));
    debugLogger.println(DebugLogger::BATTERY,
                        "Min voltage : " + String(mem_set_min_voltage));
    debugLogger.println(DebugLogger::BATTERY,
                        "Max voltage : " + String(mem_set_max_voltage));
    debugLogger.println(DebugLogger::BATTERY,
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
  if (abs(current) > mem_set_max_current ||
      current < -mem_set_max_charge_current) {
    debugLogger.println(DebugLogger::BATTERY,
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
  return !(
      compare_voltage(voltage, mem_set_max_voltage, mem_set_voltage_diff) ||
      abs(current) > mem_set_current_diff);
}

/**
 * @brief Vérifier l'état de la batterie.
 * @param INA_num Numéro de l'INA.
 * @return true si la batterie est en bon état, false sinon.
 */
bool BATTParallelator::check_battery_status(int INA_num) {
  float voltage = inaHandler.read_volt(INA_num);
  float current = inaHandler.read_current(INA_num);

  // Vérifier si la tension est valide
  if (voltage <= 1) {
    debugLogger.println(DebugLogger::BATTERY, "Battery " + String(INA_num + 1) +
                                                  " voltage is invalid.");
    return false;
  }

  // Vérifier les seuils de tension, de courant et les différences
  if (is_voltage_within_range(voltage) && is_current_within_range(current) &&
      is_difference_acceptable(voltage, current)) {
    debugLogger.println(DebugLogger::BATTERY, "Battery " + String(INA_num + 1) +
                                                  " is in good condition.");
    return true;
  } else {
    debugLogger.println(DebugLogger::BATTERY, "Battery " + String(INA_num + 1) +
                                                  " is not in good condition.");
    return false;
  }
}