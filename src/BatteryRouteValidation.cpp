#include "BatteryRouteValidation.h"

#include <cctype>
#include <cstdlib>
#include <cmath>

#ifdef ARDUINO
#include "INA_NRJ_lib.h"
#include "I2CMutex.h"
extern INAHandler inaHandler;
#endif

bool parseBatteryIndex(const char *rawArg, int maxBatteries, int &outIndex) {
  if (maxBatteries <= 0) {
    return false;
  }

  if (rawArg == nullptr) {
    return false;
  }

  const char *start = rawArg;
  while (*start != '\0' && std::isspace(static_cast<unsigned char>(*start))) {
    ++start;
  }
  if (*start == '\0') {
    return false;
  }

  char *endPtr = nullptr;
  const long parsed = strtol(start, &endPtr, 10);
  if (endPtr == start) {
    return false;
  }

  while (*endPtr != '\0' && std::isspace(static_cast<unsigned char>(*endPtr))) {
    ++endPtr;
  }
  if (*endPtr != '\0') {
    return false;
  }

  if (parsed < 0 || parsed >= maxBatteries) {
    return false;
  }

  outIndex = static_cast<int>(parsed);
  return true;
}

#ifdef ARDUINO
// TASK-007: Voltage precondition validation for safe web-initiated switch operations
bool validateBatteryVoltageForSwitch(int batteryIndex, float minVoltageMv, 
                                     float maxVoltageMv) {
  // Bounds check
  if (batteryIndex < 0 || batteryIndex >= inaHandler.getNbINA()) {
    return false;
  }
  
  if (minVoltageMv >= maxVoltageMv || minVoltageMv <= 0 || maxVoltageMv <= 0) {
    return false;
  }

  // Read battery voltage with I2C protection
  I2CLockGuard lock;
  if (!lock.isAcquired()) {
    return false; // Assume unsafe if we can't acquire lock (I2C busy)
  }

  const float voltageV = inaHandler.read_volt(batteryIndex); // Returns voltage in volts
  if (std::isnan(voltageV)) {
    return false; // Unsafe — sensor reading failed
  }

  const float voltageMv = voltageV * 1000.0f;
  
  // Voltage must be within [minVoltageMv, maxVoltageMv] to be safe for switching
  if (voltageMv < minVoltageMv || voltageMv > maxVoltageMv) {
    return false;
  }

  return true;
}
#else
// Stub for host/test environments
bool validateBatteryVoltageForSwitch(int batteryIndex, float minVoltageMv, 
                                     float maxVoltageMv) {
  (void)batteryIndex;
  (void)minVoltageMv;
  (void)maxVoltageMv;
  return true; // Stub : allow in test environment
}
#endif

