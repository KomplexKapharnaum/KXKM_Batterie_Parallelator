#include "Batt_Parallelator_lib.h"

extern const bool print_message;      // Ajouter cette ligne
extern BatteryManager batteryManager; // Ajouter cette ligne

BATTParallelator::BATTParallelator()
    : Nb_switch_max(5), max_discharge_current(1000), max_charge_current(1000) // Initialiser les courants max
{
    memset(Nb_switch, 0, sizeof(Nb_switch));
    memset(reconnect_time, 0, sizeof(reconnect_time));
}

void BATTParallelator::set_nb_switch_on(int nb) { Nb_switch_max = nb; }

void BATTParallelator::set_reconnect_delay(int delay) {
  reconnect_delay = delay;
}

void BATTParallelator::set_max_voltage(float voltage) { max_voltage = voltage; }

void BATTParallelator::set_min_voltage(float voltage) { min_voltage = voltage; }

void BATTParallelator::set_max_diff_voltage(float diff) { voltage_diff = diff; }

void BATTParallelator::set_max_diff_current(float diff) { current_diff = diff; }

void BATTParallelator::set_max_current(float current) { max_current = current; }

void BATTParallelator::set_max_discharge_current(float current)
{
    max_discharge_current = current;
}

void BATTParallelator::set_max_charge_current(float current)
{
    max_charge_current = current;
}

void BATTParallelator::switch_off_battery(int TCA_num, int OUT_num,
                                          int switch_number) {
  tcaHandler.write(TCA_num, OUT_num, 0);         // switch off the battery
  tcaHandler.write(TCA_num, OUT_num * 2 + 8, 1); // set red led on
  tcaHandler.write(TCA_num, OUT_num * 2 + 9, 0); // set green led off
}

void BATTParallelator::switch_on_battery(int TCA_num, int OUT_num) {
  tcaHandler.write(TCA_num, OUT_num, 1);         // switch on the battery
  tcaHandler.write(TCA_num, OUT_num * 2 + 8, 0); // set red led off
  tcaHandler.write(TCA_num, OUT_num * 2 + 9, 1); // set green led on
}

uint8_t BATTParallelator::TCA_num(int INA_num) {
  int address = inaHandler.getDeviceAddress(INA_num);
  if (address >= 64 && address <= 79) {
    return (address - 64) / 4;
  } else {
    return 10; // return 10 if the address is not in the range
  }
}

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

bool BATTParallelator::check_charge_status(int INA_num) {
  float current = inaHandler.read_current(INA_num);
  if (current < 0) {
    return true;
  } else {
    return false;
  }
}

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

bool BATTParallelator::compare_voltage(float voltage, float voltage_max, float diff) {
    return voltage < voltage_max - diff;
}

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

void BATTParallelator::reset_switch_count(int INA_num) {
  Nb_switch[INA_num] = 0;
  reconnect_time[INA_num] = 0;
}
