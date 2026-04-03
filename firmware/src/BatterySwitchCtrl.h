#ifndef KXKM_ALLOW_LEGACY_HEADERS
#error "Legacy header BatterySwitchCtrl.h is deprecated. Use BATTParallelator/TCAHandler APIs instead. Define KXKM_ALLOW_LEGACY_HEADERS only for explicit migration tasks."
#endif

// Nb_switch_max défini dans config.h
int Nb_switch[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int check_switch[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void switch_off_battery(int TCA_num, int OUT_num, int switch_number) // switch off the battery
{
    TCA_write(TCA_num, OUT_num, 0); // switch off the battery
    TCA_write(TCA_num, OUT_num * 2 + 8, 1);
    TCA_write(TCA_num, OUT_num * 2 + 9, 0);
    if (check_switch[switch_number] == 0)
    {
        Nb_switch[switch_number]++;
        check_switch[switch_number] = 1;
    }
}

void switch_on_battery(int TCA_num, int OUT_num) // switch on the battery
{
    TCA_write(TCA_num, OUT_num, 1);
    TCA_write(TCA_num, OUT_num * 2 + 8, 0);
    TCA_write(TCA_num, OUT_num * 2 + 9, 1);
}