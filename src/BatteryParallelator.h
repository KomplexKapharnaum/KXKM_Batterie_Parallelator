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
 * @file BatteryParallelator.h
 * @brief Déclaration des classes BATTParallelator et BatteryManager pour gérer
 * les batteries en parallèle.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
 */

#ifndef BATTERY_PARALLELATOR_H
#define BATTERY_PARALLELATOR_H

#include "INAHandler.h"
#include "TCAHandler.h"
#include <KxLogger.h>
#include <cfloat>
#include <cmath>
#include <freertos/semphr.h>
#include "BatteryManager.h" // Inclure le nouvel en-tête

extern TCAHandler tcaHandler;
extern INAHandler inaHandler;

class BatteryManager; // Garder uniquement cette déclaration si nécessaire

/**
 * @class BATTParallelator
 * @brief Classe pour gérer les batteries en parallèle.
 */
class BATTParallelator {
public:
  BATTParallelator();
  bool switch_off_battery(int INA_num);
  bool switch_on_battery(int INA_num);
  uint8_t TCA_num(int INA_num);
  bool check_battery_status(int INA_num);
  bool check_charge_status(int INA_num);
  bool switch_battery(int INA_num, bool switch_on);
  void check_battery_connected_status(int INA_num);
  void reset_switch_count(int INA_num);
  int detect_batteries();
  float battery_voltages[16];
  void set_battery_voltage(int INA_num, float voltage);
  void copy_battery_voltages(float *dest, int count);

  // Setters
  void set_max_voltage(float voltage);
  void set_min_voltage(float voltage);
  void set_max_current(float current);
  void set_max_diff_voltage(float diff);
  void set_max_diff_current(float diff);
  void set_reconnect_delay(int delay);
  void set_nb_switch_on(int nb);
  void set_max_discharge_current(float current);
  void set_max_charge_current(float current);

  // Utility methods
  bool check_voltage_offset(int INA_num, float offset);
  bool compare_voltage(float voltage, float voltage_max, float diff);
  float find_max_voltage(float *battery_voltages, int num_batteries);
  float find_min_voltage(float *battery_voltages, int num_batteries);
  bool is_current_within_range(float current);
  bool is_voltage_within_range(float voltage);
  bool is_difference_acceptable(float voltage, float current);

private:
  bool isValidBatteryIndex(int INA_num) const;
  bool lockState(TickType_t timeout = pdMS_TO_TICKS(20));
  SemaphoreHandle_t stateMutex = NULL;
  int mem_set_max_voltage = 30000;
  int mem_set_min_voltage = 24000;
  int mem_set_max_current = 1000;
  int mem_set_voltage_diff = 2000;
  int mem_set_current_diff = 1000;
  int mem_set_max_charge_current = 0;
  int mem_set_max_discharge_current = 0;

  int reconnect_delay = 0;
  int Nb_switch_max = 5;
  int Nb_switch[16];
  long reconnect_time[16];
  int max_discharge_current;
};

struct AmpereHourTaskParams {
  int batteryIndex;
  float durationHours;
  int numSamples;
  BatteryManager *manager; // Ajouter un membre manager
};

#endif // BATTERY_PARALLELATOR_H