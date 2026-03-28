#ifndef BATTERY_ROUTE_VALIDATION_H
#define BATTERY_ROUTE_VALIDATION_H

// Validate and parse a battery index from HTTP argument text.
// Returns true only for a strict integer within [0, maxBatteries-1].
bool parseBatteryIndex(const char *rawArg, int maxBatteries, int &outIndex);

// TASK-007: Validate battery voltage precondition for safe switch operations.
// Prevents switching when voltage is out of safe range (e.g., during brownout or overvoltage).
// minVoltageMv and maxVoltageMv are in millivolts (e.g., 24000-30000 mV for 24-30V nominal).
// Returns true if voltage is within [minVoltageMv, maxVoltageMv], false otherwise.
bool validateBatteryVoltageForSwitch(int batteryIndex, float minVoltageMv, float maxVoltageMv);

#endif // BATTERY_ROUTE_VALIDATION_H
