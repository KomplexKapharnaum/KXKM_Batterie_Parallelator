#ifndef BATT_PARALLELATOR_LIB_H
#define BATT_PARALLELATOR_LIB_H

#include "INA_NRJ_lib.h"
#include "TCA_NRJ_lib.h"
class BATTParallelator
{
public:
    BATTParallelator();
    void switch_off_battery(int TCA_num, int OUT_num, int switch_number);
    void switch_on_battery(int TCA_num, int OUT_num);
    uint8_t TCA_num(int INA_num);
    bool check_battery_status(int INA_num);
    bool check_charge_status(int INA_num);
    bool switch_battery(int INA_num, bool switch_on);
    void check_battery_connected_status(int INA_num);
    void set_max_voltage(float voltage);
    void set_min_voltage(float voltage);
    void set_max_current(float current);
    void set_max_charge_current(float current);
    void set_max_diff_voltage(float diff);
    void set_max_diff_current(float diff);
    void set_reconnect_delay(int delay);
    void set_nb_switch_on(int nb);
    void reset_switch_count(int INA_num);
    float compare_voltage(float voltage, float voltage_max, float diff);
    float find_max_voltage(float *battery_voltages, int num_batteries);
    float find_min_voltage(float *battery_voltages, int num_batteries);

private:
    int max_voltage = 30000; // Battery undervoltage threshold in mV;
    int min_voltage = 24000; // Battery overvoltage threshold in mV
    int max_current = 1000;  // Battery overcurrent threshold in mA
    int voltage_diff = 2000; // Battery voltage difference threshold in mV
    int current_diff = 1000; // Battery current difference threshold in mA
    int max_charge_current;  // Battery charge current threshold in mA
    int reconnect_delay;     // delay to reconnect the battery in ms
    int Nb_switch_max;       // number of switch on before disconnect the battery
    int Nb_switch[16];
    long reconnect_time[16];
};

extern TCAHandler tcaHandler;
extern INAHandler inaHandler;

BATTParallelator::BATTParallelator()
    : Nb_switch_max(5)
{
    memset(Nb_switch, 0, sizeof(Nb_switch));
    memset(reconnect_time, 0, sizeof(reconnect_time));
}

/**
 * @brief Set the number of switch on before disconnect the battery.
 * 
 * @param nb Number of switch on.
 */
void BATTParallelator::set_nb_switch_on(int nb)
{
    Nb_switch_max = nb;
}

/**
 * @brief Set the delay to reconnect the battery in ms.
 * 
 * @param delay Reconnect delay.
 */
void BATTParallelator::set_reconnect_delay(int delay)
{
    reconnect_delay = delay;
}

/**
 * @brief Set the max voltage in mV.
 * 
 * @param voltage Max voltage.
 */
void BATTParallelator::set_max_voltage(float voltage)
{
    max_voltage = voltage;
}

/**
 * @brief Set the min voltage in mV.
 * 
 * @param voltage Min voltage.
 */
void BATTParallelator::set_min_voltage(float voltage)
{
    min_voltage = voltage;
}

/**
 * @brief Set the max voltage difference in mV.
 * 
 * @param diff Max voltage difference.
 */
void BATTParallelator::set_max_diff_voltage(float diff)
{
    voltage_diff = diff;
}

/**
 * @brief Set the max current difference in mA.
 * 
 * @param diff Max current difference.
 */
void BATTParallelator::set_max_diff_current(float diff)
{
    current_diff = diff;
}

/**
 * @brief Set the max current in mA.
 * 
 * @param current Max current.
 */
void BATTParallelator::set_max_current(float current)
{
    max_current = current;
}

/**
 * @brief Set the max charge current in mA.
 * 
 * @param current Max charge current.
 */
void BATTParallelator::set_max_charge_current(float current)
{
    max_charge_current = current;
}

/**
 * @brief Switch off the battery.
 * 
 * @param TCA_num TCA number.
 * @param OUT_num Output number.
 * @param switch_number Switch number.
 */
void BATTParallelator::switch_off_battery(int TCA_num, int OUT_num, int switch_number)
{
    tcaHandler.write(TCA_num, OUT_num, 0);         // switch off the battery
    tcaHandler.write(TCA_num, OUT_num * 2 + 8, 1); // set red led on
    tcaHandler.write(TCA_num, OUT_num * 2 + 9, 0); // set green led off
}

/**
 * @brief Switch on the battery.
 * 
 * @param TCA_num TCA number.
 * @param OUT_num Output number.
 */
void BATTParallelator::switch_on_battery(int TCA_num, int OUT_num)
{
    tcaHandler.write(TCA_num, OUT_num, 1);         // switch on the battery
    tcaHandler.write(TCA_num, OUT_num * 2 + 8, 0); // set red led off
    tcaHandler.write(TCA_num, OUT_num * 2 + 9, 1); // set green led on
}

/**
 * @brief Get the TCA number of the INA device number corresponding to battery number.
 * 
 * @param INA_num INA number.
 * @return uint8_t TCA number.
 */
uint8_t BATTParallelator::TCA_num(int INA_num)
{
    int address = inaHandler.getDeviceAddress(INA_num);
    if (address >= 64 && address <= 79)
    {
        return (address - 64) / 4; 
    }
    else
    {
        return 10; // return 10 if the address is not in the range
    }
}

/**
 * @brief Check battery status and return true if the battery specs are respected.
 * 
 * @param INA_num INA number.
 * @return true If battery specs are respected.
 * @return false If battery specs are not respected.
 */
bool BATTParallelator::check_battery_status(int INA_num)
{
    float voltage = inaHandler.read_volt(INA_num);
    float current = inaHandler.read_current(INA_num);

    if (voltage < min_voltage / 1000) // check if the battery voltage is too low
    {
        if (print_message)
            Serial.println("Battery " + String(INA_num) + "voltage is too low");
        return false;
    }
    else if (voltage > max_voltage / 1000) // check if the battery voltage is too high
    {
        if (print_message)
            Serial.println("Battery " + String(INA_num) + "voltage is too high");
        return false;
    }
    else if (current > max_current || current < -max_current) // check if the battery current is too high
    {
        if (print_message)
            Serial.println("Battery " + String(INA_num) + "current is too high");
        return false;
    }
    else if (current < -max_charge_current) // check if the battery charge current is too high
    {
        if (print_message)
            Serial.println("Battery " + String(INA_num) + "charge current is too high");
        return false;
    }
    else if (compare_voltage(voltage, max_voltage, voltage_diff)) // check if the battery voltage is different from the others //TODO check this function
    {
        if (print_message)
            Serial.println("Voltage batterie " + String(INA_num) + "is different");
        return false;
    }
    else
    {
        return true;
    }
}

/**
 * @brief Check battery charge status and return true if the battery is charging.
 * 
 * @param INA_num INA number.
 * @return true If battery is charging.
 * @return false If battery is not charging.
 */
bool BATTParallelator::check_charge_status(int INA_num)
{
    float current = inaHandler.read_current(INA_num);
    if (current < 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}

/**
 * @brief Switch on/off the battery.
 * 
 * @param INA_num INA number.
 * @param switch_on Switch on or off.
 * @return true If switched on.
 * @return false If switched off.
 */
bool BATTParallelator::switch_battery(int INA_num, bool switch_on)
{
    int TCA_number = TCA_num(INA_num);
    int OUT_number = (inaHandler.getDeviceAddress(INA_num) - 64) % 4;
    if (switch_on)
    {
        switch_on_battery(TCA_number, OUT_number);
        return true;
    }
    else
    {
        switch_off_battery(TCA_number, OUT_number, INA_num);
        return false;
    }
}

/**
 * @brief Check battery status and switch on/off if needed and possible (max switch number not reached).
 * 
 * @param INA_num INA number.
 */
void BATTParallelator::check_battery_connected_status(int INA_num)
{
    if (check_battery_status(INA_num))
    {
        if (Nb_switch[INA_num] < 1)
        {
            if (print_message)
                Serial.println("Battery voltage and current are good");
            switch_on_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4); // switch on the battery
            Nb_switch[INA_num]++;
        }
        else if (Nb_switch[INA_num] < Nb_switch_max)
        {
            if (print_message)
                Serial.println("cut off battery, try to reconnect in " + String(reconnect_delay / 1000) + "s");
            switch_off_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4, INA_num); // switch off the battery
            if (reconnect_time[INA_num] == 0)
            {
                reconnect_time[INA_num] = millis();
            }
            if ((millis() - reconnect_time[INA_num] > reconnect_delay) && (Nb_switch[INA_num] < Nb_switch_max)) // reconnect the battery after the delay
            {
                switch_on_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4); // switch on the battery
                Nb_switch[INA_num]++;                                                                 // increment the number of switch on
            }
        }
        else
        {
            if (print_message)
                Serial.println("Battery is disconnected");
            switch_off_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4, INA_num); // switch off the battery
        }
    }
}

/**
 * @brief Compare the voltage with a threshold.
 * 
 * @param voltage Voltage to compare.
 * @param voltage_max Max voltage.
 * @param diff Voltage difference.
 * @return float Result of comparison.
 */
float BATTParallelator::compare_voltage(float voltage, float voltage_max, float diff)
{
    if (voltage < voltage_max - diff)
    {
        return true;
    }
    else
    {
        return false;
    }
}

/**
 * @brief Find the max voltage in an array of voltages.
 * 
 * @param battery_voltages Array of battery voltages.
 * @param num_batteries Number of batteries.
 * @return float Max voltage.
 */
float BATTParallelator::find_max_voltage(float *battery_voltages, int num_batteries)
{
    for (int i = 1; i < inaHandler.getNbINA(); i++)
    {
        battery_voltages[i] = inaHandler.read_volt(i);
        if (battery_voltages[i] > max_voltage)
        {
            max_voltage = battery_voltages[i];
        }
    }
    return max_voltage;
}

/**
 * @brief Find the min voltage in an array of voltages.
 * 
 * @param battery_voltages Array of battery voltages.
 * @param num_batteries Number of batteries.
 * @return float Min voltage.
 */
float BATTParallelator::find_min_voltage(float *battery_voltages, int num_batteries)
{
    float min_voltage = battery_voltages[num_batteries];
    for (int i = 1; i < inaHandler.getNbINA(); i++)
    {
        if (battery_voltages[i] < min_voltage)
        {
            min_voltage = battery_voltages[i];
        }
    }
    return min_voltage;
}

/**
 * @brief Reset the switch count for a specific battery.
 * 
 * @param INA_num INA number.
 */
void BATTParallelator::reset_switch_count(int INA_num)
{
    Nb_switch[INA_num] = 0;
    reconnect_time[INA_num] = 0;
}

#endif // BATT_PARALLELATOR_LIB_H