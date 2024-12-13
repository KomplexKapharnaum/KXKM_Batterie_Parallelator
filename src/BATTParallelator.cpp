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
 * @file BATTParallelator.cpp
 * @brief Implémentation de la classe BATTParallelator pour gérer les batteries en parallèle.
 * 
 * Ce fichier contient les définitions des méthodes de la classe BATTParallelator,
 * qui permet de gérer les batteries en parallèle, y compris la vérification de l'état,
 * la commutation et la gestion des courants et tensions.
 */

#include "Batt_Parallelator_lib.h"

extern const bool print_message;      // Ajouter cette ligne
extern BatteryManager batteryManager; // Ajouter cette ligne

/**
 * @brief Constructeur de la classe BATTParallelator.
 */
BATTParallelator::BATTParallelator()
    : Nb_switch_max(5), max_discharge_current(1000), max_charge_current(1000) // Initialiser les courants max
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
void BATTParallelator::set_max_voltage(float voltage) { max_voltage = voltage; }

/**
 * @brief Définir la tension minimale.
 * @param voltage Tension minimale en volts.
 */
void BATTParallelator::set_min_voltage(float voltage) { min_voltage = voltage; }

/**
 * @brief Définir la différence de tension maximale.
 * @param diff Différence de tension maximale en volts.
 */
void BATTParallelator::set_max_diff_voltage(float diff) { voltage_diff = diff; }

/**
 * @brief Définir la différence de courant maximale.
 * @param diff Différence de courant maximale en ampères.
 */
void BATTParallelator::set_max_diff_current(float diff) { current_diff = diff; }

/**
 * @brief Définir le courant maximal.
 * @param current Courant maximal en ampères.
 */
void BATTParallelator::set_max_current(float current) { max_current = current; }

/**
 * @brief Définir le courant de décharge maximal.
 * @param current Courant de décharge maximal en ampères.
 */
void BATTParallelator::set_max_discharge_current(float current)
{
    max_discharge_current = current;
}

/**
 * @brief Définir le courant de charge maximal.
 * @param current Courant de charge maximal en ampères.
 */
void BATTParallelator::set_max_charge_current(float current)
{
    max_charge_current = current;
}

/**
 * @brief Éteindre une batterie.
 * @param TCA_num Numéro du TCA.
 * @param OUT_num Numéro de sortie.
 * @param switch_number Numéro du commutateur.
 */
void BATTParallelator::switch_off_battery(int TCA_num, int OUT_num,
                                          int switch_number) {
  tcaHandler.write(TCA_num, OUT_num, 0);         // switch off the battery
  tcaHandler.write(TCA_num, OUT_num * 2 + 8, 1); // set red led on
  tcaHandler.write(TCA_num, OUT_num * 2 + 9, 0); // set green led off
}

/**
 * @brief Allumer une batterie.
 * @param TCA_num Numéro du TCA.
 * @param OUT_num Numéro de sortie.
 */
void BATTParallelator::switch_on_battery(int TCA_num, int OUT_num) {
  tcaHandler.write(TCA_num, OUT_num, 1);         // switch on the battery
  tcaHandler.write(TCA_num, OUT_num * 2 + 8, 0); // set red led off
  tcaHandler.write(TCA_num, OUT_num * 2 + 9, 1); // set green led on
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
 * @brief Vérifier l'état de la batterie.
 * @param INA_num Numéro de l'INA.
 * @return true si la batterie est en bon état, false sinon.
 */
bool BATTParallelator::check_battery_status(int INA_num) {
  float voltage = inaHandler.read_volt(INA_num);
  float current = inaHandler.read_current(INA_num);
  bool return_value;
  if (voltage < min_voltage / 1000) // check if the battery voltage is too low
  {
    if (print_message)
      Serial.println("Battery " + String(INA_num) + "voltage is too low");
    return_value = false;
  } else if (voltage >
             max_voltage / 1000) // check if the battery voltage is too high
  {
    if (print_message)
      Serial.println("Battery " + String(INA_num) + "voltage is too high");
    return_value = false;
  } else if (current > max_current ||
             current < -max_current) // check if the battery current is too high
  {
    if (print_message)
      Serial.println("Battery " + String(INA_num) + "current is too high");
    return_value = false;
  } else if (current < -max_charge_current) // check if the battery charge
                                            // current is too high
  {
    if (print_message)
      Serial.println("Battery " + String(INA_num) +
                     "charge current is too high");
    return_value = false;
  } else if (compare_voltage(
                 voltage, max_voltage,
                 voltage_diff)) // check if the battery voltage is different
                                // from the others //TODO check this function
  {
    if (print_message)
      Serial.println("Voltage batterie " + String(INA_num) + "is different");
    return_value = false;
  } else if (abs(current) > current_diff) // check if the battery current is
                                          // different from the others
  {
    if (print_message)
      Serial.println("Current battery " + String(INA_num) + "is different");
    return_value = false;
  } else if (check_charge_status(INA_num)) // check if the battery is charging
  {
    if (print_message)
      Serial.println("Battery " + String(INA_num) + "is charging");
    return_value = true;
  } else if (batteryManager.getAmpereHourConsumption(INA_num) >
             0) // check if the battery is discharging
  {
    if (print_message)
      Serial.println("Battery " + String(INA_num) + "is discharging");
    return_value = true;
  } else if (batteryManager.getVoltageCurrentRatio(INA_num) >
             0) // check if the battery is discharging
  {
    if (print_message)
      Serial.println("Battery " + String(INA_num) + "is discharging");
    return_value = true;
  } else if (batteryManager.getVoltageCurrentRatio(INA_num) <
             0) // check if the battery is charging
  {
    if (print_message)
      Serial.println("Battery " + String(INA_num) + "is charging");
    return_value = true;
  } else if (batteryManager.getVoltageCurrentRatio(INA_num) ==
             0) // check if the battery is not charging or discharging
  {
    if (print_message)
      Serial.println("Battery " + String(INA_num) +
                     "is not charging or discharging");
    return_value = true;
  } else if (check_voltage_offset(INA_num,
                                  0.5)) // TODO check this function with better
                                        // offset or calculation of the offset
  {
    if (print_message)
      Serial.println("Battery " + String(INA_num) +
                     "voltage is in offset range");
    return_value = true;
  } else {
    return_value = true;
  }
  return return_value;
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
    if (print_message) {
      Serial.print("Battery ");
      Serial.print(INA_num);
      Serial.print(" voltage is out of offset range: ");
      Serial.println(voltage);
    }
    return false;
  }
  return true;
}

/**
 * @brief Vérifier l'état de charge de la batterie.
 * @param INA_num Numéro de l'INA.
 * @return true si la batterie est en charge, false sinon.
 */
bool BATTParallelator::check_charge_status(int INA_num) {
  float current = inaHandler.read_current(INA_num);
  if (current < 0) {
    return true;
  } else {
    return false;
  }
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
  if (switch_on) {
    switch_on_battery(TCA_number, OUT_number);
    return true;
  } else {
    switch_off_battery(TCA_number, OUT_number, INA_num);
    return false;
  }
}

/**
 * @brief Vérifier l'état de connexion de la batterie.
 * @param INA_num Numéro de l'INA.
 */
void BATTParallelator::check_battery_connected_status(int INA_num) {
  const float voltageOffset = 0.5; // Définir l'offset de tension en volts

  if (check_battery_status(INA_num) &&
      check_voltage_offset(INA_num, voltageOffset)) {
    if (Nb_switch[INA_num] < 1) {
      if (print_message)
        Serial.println("Battery voltage and current are good");
      switch_on_battery(TCA_num(INA_num),
                        (inaHandler.getDeviceAddress(INA_num) - 64) %
                            4); // switch on the battery
      Nb_switch[INA_num]++;
    } else if (Nb_switch[INA_num] < Nb_switch_max) {
      if (print_message)
        Serial.println("cut off battery, try to reconnect in " +
                       String(reconnect_delay / 1000) + "s");
      switch_off_battery(TCA_num(INA_num),
                         (inaHandler.getDeviceAddress(INA_num) - 64) % 4,
                         INA_num); // switch off the battery
      if (reconnect_time[INA_num] == 0) {
        reconnect_time[INA_num] = millis();
      }
      if ((millis() - reconnect_time[INA_num] > reconnect_delay) &&
          (Nb_switch[INA_num] <
           Nb_switch_max)) // reconnect the battery after the delay
      {
        switch_on_battery(TCA_num(INA_num),
                          (inaHandler.getDeviceAddress(INA_num) - 64) %
                              4); // switch on the battery
        Nb_switch[INA_num]++;     // increment the number of switch on
      }
    } else {
      if (print_message)
        Serial.println("Battery is disconnected");
      switch_off_battery(TCA_num(INA_num),
                         (inaHandler.getDeviceAddress(INA_num) - 64) % 4,
                         INA_num); // switch off the battery
    }
  }
}

/**
 * @brief Comparer la tension de la batterie avec une tension maximale.
 * @param voltage Tension de la batterie.
 * @param voltage_max Tension maximale.
 * @param diff Différence de tension acceptable.
 * @return true si la tension de la batterie est inférieure à la tension maximale moins la différence, false sinon.
 */
bool BATTParallelator::compare_voltage(float voltage, float voltage_max, float diff) {
  if (voltage < voltage_max - diff) {
    return true;
  } else {
    return false;
  }
}

/**
 * @brief Trouver la tension maximale parmi les batteries.
 * @param battery_voltages Tableau des tensions des batteries.
 * @param num_batteries Nombre de batteries.
 * @return Tension maximale.
 */
float BATTParallelator::find_max_voltage(float *battery_voltages,
                                         int num_batteries) {
  for (int i = 1; i < inaHandler.getNbINA(); i++) {
    battery_voltages[i] = inaHandler.read_volt(i);
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
  float min_voltage = battery_voltages[num_batteries];
  for (int i = 1; i < inaHandler.getNbINA(); i++) {
    if (battery_voltages[i] < min_voltage) {
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
