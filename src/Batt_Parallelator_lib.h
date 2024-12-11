#ifndef BATT_PARALLELATOR_LIB_H
#define BATT_PARALLELATOR_LIB_H

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
    void set_reconnect_delay(int delay);
    void set_nb_switch_on(int nb);

private:
    int max_voltage = 30000; // Battery undervoltage threshold in mV;
    int min_voltage = 24000; // Battery overvoltage threshold in mV
    int max_current = 1000;  // Battery overcurrent threshold in mA
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

void BATTParallelator::set_nb_switch_on(int nb) // set the number of switch on before disconnect the battery
{
    Nb_switch_max = nb;
}

void BATTParallelator::set_reconnect_delay(int delay) // set the delay to reconnect the battery in ms
{
    reconnect_delay = delay;
}

void BATTParallelator::set_max_voltage(float voltage) // set the max voltage in mV
{
    max_voltage = voltage;
}

void BATTParallelator::set_min_voltage(float voltage) // set the min voltage in mV
{
    min_voltage = voltage;
}

void BATTParallelator::set_max_current(float current) // set the max current in mA
{
    max_current = current;
}

void BATTParallelator::set_max_charge_current(float current) // set the max charge current in mA
{
    max_charge_current = current;
}

void BATTParallelator::switch_off_battery(int TCA_num, int OUT_num, int switch_number) // switch off the battery
{
    tcaHandler.write(TCA_num, OUT_num, 0);         // switch off the battery
    tcaHandler.write(TCA_num, OUT_num * 2 + 8, 1); // set red led on
    tcaHandler.write(TCA_num, OUT_num * 2 + 9, 0); // set green led off
}

void BATTParallelator::switch_on_battery(int TCA_num, int OUT_num) // switch on the battery
{
    tcaHandler.write(TCA_num, OUT_num, 1);         // switch on the battery
    tcaHandler.write(TCA_num, OUT_num * 2 + 8, 0); // set red led off
    tcaHandler.write(TCA_num, OUT_num * 2 + 9, 1); // set green led on
}

uint8_t BATTParallelator::TCA_num(int INA_num) // get the TCA number of the INA device number corresponding to battery number
{
    int address = inaHandler.getDeviceAddress(INA_num);
    if (address >= 64 && address <= 79)
    {
        return (address - 64) / 4;
    }
    else
    {
        return 10;
    }
}

bool BATTParallelator::check_battery_status(int INA_num) // check battery status and return true if the battery specs are respected
{
    float voltage = inaHandler.read_volt(INA_num);
    float current = inaHandler.read_current(INA_num);

    if (voltage < min_voltage / 1000) // check if the battery voltage is too low
    {
        if (print_message)
            Serial.println("Battery voltage is too low");
        return false;
    }
    else if (voltage > max_voltage / 1000) // check if the battery voltage is too high
    {
        if (print_message)
            Serial.println("Battery voltage is too high");
        return false;
    }
    else if (current > max_current || current < -max_current) // check if the battery current is too high
    {
        if (print_message)
            Serial.println("Battery current is too high");
        return false;
    }
    else if (current < -max_charge_current) // check if the battery charge current is too high
    {
        if (print_message)
            Serial.println("Battery charge current is too high");
        return false;
    }
    else
    {
        return true;
    }
}

bool BATTParallelator::check_charge_status(int INA_num) // check battery charge status and return true if the battery is charging
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

bool BATTParallelator::switch_battery(int INA_num, bool switch_on) // switch on/off the battery
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

void BATTParallelator::check_battery_connected_status(int INA_num) // check battery status and switch on/off if needed and possible (max switch number not reached)
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
            switch_off_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4, INA_num);
            if (reconnect_time[INA_num] == 0)
            {
                reconnect_time[INA_num] = millis();
            }
            if ((millis() - reconnect_time[INA_num] > reconnect_delay) && (Nb_switch[INA_num] < Nb_switch_max))
            {
                switch_on_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4);
                Nb_switch[INA_num]++;
            }
        }
        else
        {
            if (print_message)
                Serial.println("Battery is disconnected");
            switch_off_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4, INA_num);
        }
    }
}

#endif // BATT_PARALLELATOR_LIB_H