int Nb_switch_max = 5;
int Nb_switch[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int check_switch[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
long reconnect_time[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

extern TCAHandler tcaHandler;
extern INAHandler inaHandler;

void switch_off_battery(int TCA_num, int OUT_num, int switch_number) // switch off the battery
{
    tcaHandler.write(TCA_num, OUT_num, 0);         // switch off the battery
    tcaHandler.write(TCA_num, OUT_num * 2 + 8, 1); // set red led on
    tcaHandler.write(TCA_num, OUT_num * 2 + 9, 0); // set green led off
    if (check_switch[switch_number] == 0)
    {
        Nb_switch[switch_number]++;
        check_switch[switch_number] = 1;
    }
}

void switch_on_battery(int TCA_num, int OUT_num) // switch on the battery
{
    tcaHandler.write(TCA_num, OUT_num, 1);         // switch on the battery
    tcaHandler.write(TCA_num, OUT_num * 2 + 8, 0); // set red led off
    tcaHandler.write(TCA_num, OUT_num * 2 + 9, 1); // set green led on
}

uint8_t TCA_num(int INA_num) // return TCA number about INA number
{
    if ((inaHandler.getDeviceAddress(INA_num) >= 64) && (inaHandler.getDeviceAddress(INA_num) <= 67))
    {
        return 0;
    }
    if ((inaHandler.getDeviceAddress(INA_num) >= 68) && (inaHandler.getDeviceAddress(INA_num) <= 71))
    {
        return 1;
    }
    if ((inaHandler.getDeviceAddress(INA_num) >= 72) && (inaHandler.getDeviceAddress(INA_num) <= 75))
    {
        return 2;
    }
    if ((inaHandler.getDeviceAddress(INA_num) >= 76) && (inaHandler.getDeviceAddress(INA_num) <= 79))
    {
        return 3;
    }
    else
    {
        return 10;
    }
}

bool check_battery_status(int INA_num) // check battery status
{
    if (inaHandler.read_volt(INA_num) < alert_bat_min_voltage / 1000)
    {
        if (print_message)
            Serial.println("Battery voltage is too low");
        return false;
    }
    else if (inaHandler.read_volt(INA_num) > alert_bat_max_voltage / 1000)
    {
        if (print_message)
            Serial.println("Battery voltage is too high");
        return false;
    }
    else if (inaHandler.read_current(INA_num) > alert_bat_max_current)
    {
        if (print_message)
            Serial.println("Battery current is too high");
        return false;
    }
    else if (inaHandler.read_current(INA_num) < -alert_bat_max_current)
    {
        if (print_message)
            Serial.println("Battery current is too high");
        return false;
    }
    else
    {
        return true;
    }
}

bool switch_battery(int INA_num, bool switch_on) // return value of the battery are on or off
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

void check_battery(int INA_num) // check the battery status and switch on or off the battery
{
    if (check_battery_status(INA_num)) // check battery status
    {
        check_switch[INA_num] = 0;              // reset the switch counter
        if (Nb_switch[INA_num] < Nb_switch_max) // check the number of switch
        {
            if (print_message)
                Serial.println("Battery voltage and current are good");
            switch_on_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4); // switch on the battery
        }
        else if (Nb_switch[INA_num] >= Nb_switch_max) // check the number of switching
        {

            if (print_message)
                Serial.println("too many cut off battery, try to reconnect in" + String(reconnect_delay / 1000) + "s");
            switch_off_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4, INA_num);
            if (reconnect_time[INA_num] == 0) // check the time to reconnect
            {
                reconnect_time[INA_num] = millis();
            }

            if ((millis() - reconnect_time[INA_num] > reconnect_delay) && (Nb_switch[INA_num] == Nb_switch_max)) // check the time to reconnect
            {
                switch_on_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4);
                Nb_switch[INA_num]++;
                check_switch[INA_num] = 0;
            }
        }
        else if (Nb_switch[INA_num] > Nb_switch_max + 1) // check the number of switching max
        {
            if (print_message)
                Serial.println("too many cut off battery, try to reconnect in" + String(reconnect_delay / 1000) + "s");
            switch_off_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4, INA_num);
            if (reconnect_time[INA_num] == 0)
            {
                reconnect_time[INA_num] = millis();
            }
            if ((millis() - reconnect_time[INA_num] > reconnect_delay) && (Nb_switch[INA_num] == Nb_switch_max))
            {
                switch_on_battery(TCA_num(INA_num), (inaHandler.getDeviceAddress(INA_num) - 64) % 4);
                Nb_switch[INA_num]++;
                check_switch[INA_num] = 0;
            }
        }
    }
}
