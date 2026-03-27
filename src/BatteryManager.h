#ifndef BATTERY_MANAGER_H
#define BATTERY_MANAGER_H

#include "INA_NRJ_lib.h"
#include <DebugLogger.h>
#include <cfloat>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class BatteryManager {
public:
  BatteryManager() : maxVoltage(0), minVoltage(FLT_MAX) {
    for (int i = 0; i < 16; i++) {
      ampereHourConsumptions[i] = 0.0;
      ampereHourCharges[i] = 0.0;
      ampereHourTaskRunning[i] = false;
      ampereHourTaskHandles[i] = nullptr;
    }
  }

  float getMaxVoltageBattery();
  float getMinVoltageBattery();
  float getAverageVoltage();
  float getVoltageCurrentRatio(int batteryIndex);
  void startAmpereHourConsumptionTask(int batteryIndex, float durationHours,
                                      int numSamples);
  static void ampereHourConsumptionTask(void *pvParameters);
  float getAmpereHourConsumption(int batteryIndex);
  float getAmpereHourCharge(int batteryIndex); // Charge (valeurs négatives)
  bool isAmpereHourConsumptionTaskRunning(int batteryIndex);
  float getTotalConsumption(); // Total décharge
  float getTotalCharge(); // Total charge
  float getTotalCurrent(); // Courant total instantané

  /**
   * @brief Obtenir la tension maximale parmi les batteries.
   * @return La tension maximale.
   */
  float getMaxVoltage();

  /**
   * @brief Obtenir la tension minimale parmi les batteries.
   * @return La tension minimale.
   */
  float getMinVoltage();

private:
  float ampereHourConsumptions[16] = {0}; // Décharge (valeurs positives)
  float ampereHourCharges[16] = {0};      // Charge (valeurs négatives)
  bool ampereHourTaskRunning[16] = {false}; // Tableau pour suivre les tâches en cours
  TaskHandle_t ampereHourTaskHandles[16] = {nullptr};
  float maxVoltage = 0;
  float minVoltage = FLT_MAX;
};

#endif // BATTERY_MANAGER_H
