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
 * @file Batt_Parallelator_lib.h
 * @brief Déclaration des classes BATTParallelator et BatteryManager pour gérer les batteries en parallèle.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
 */

#ifndef BATT_PARALLELATOR_LIB_H
#define BATT_PARALLELATOR_LIB_H

#include "INA_NRJ_lib.h"
#include "TCA_NRJ_lib.h"
#include <cfloat> 
#include <cmath>

class BatteryManager; // Ajouter cette ligne

/**
 * @class BATTParallelator
 * @brief Classe pour gérer les batteries en parallèle.
 */
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

    /**
     * @brief Définir la tension maximale.
     * @param voltage La tension maximale en mV.
     */
    void set_max_voltage(float voltage);

    /**
     * @brief Définir la tension minimale.
     * @param voltage La tension minimale en mV.
     */
    void set_min_voltage(float voltage);

    /**
     * @brief Définir le courant maximal.
     * @param current Le courant maximal en mA.
     */
    void set_max_current(float current);

    /**
     * @brief Définir l'offset de différence de tension.
     * @param diff L'offset de différence de tension en mV.
     */
    void set_max_diff_voltage(float diff);

    /**
     * @brief Définir l'offset de différence de courant.
     * @param diff L'offset de différence de courant en mA.
     */
    void set_max_diff_current(float diff);

    /**
     * @brief Définir le délai de reconnexion.
     * @param delay Le délai de reconnexion en ms.
     */
    void set_reconnect_delay(int delay);

    /**
     * @brief Définir le nombre de commutations avant déconnexion.
     * @param nb Le nombre de commutations.
     */
    void set_nb_switch_on(int nb);

    /**
     * @brief Réinitialiser le compteur de commutations.
     * @param INA_num Le numéro de l'INA.
     */
    void reset_switch_count(int INA_num);

    /**
     * @brief Vérifier l'offset de différence de tension.
     * @param INA_num Le numéro de l'INA.
     * @param offset L'offset de différence de tension en volts.
     * @return true si l'offset est respecté, false sinon.
     */
    bool check_voltage_offset(int INA_num, float offset);

    /**
     * @brief Définir le courant de décharge maximal.
     * @param current Le courant de décharge maximal en mA.
     */
    void set_max_discharge_current(float current);

    /**
     * @brief Définir le courant de charge maximal.
     * @param current Le courant de charge maximal en mA.
     */
    void set_max_charge_current(float current);

    bool compare_voltage(float voltage, float voltage_max, float diff);

    float find_max_voltage(float *battery_voltages, int num_batteries);
    float find_min_voltage(float *battery_voltages, int num_batteries);

private:
    int max_voltage = 30000; // Seuil de sous-tension de la batterie en mV
    int min_voltage = 24000; // Seuil de surtension de la batterie en mV
    int max_current = 1000;  // Seuil de surintensité de la batterie en mA
    int voltage_diff = 2000; // Seuil de différence de tension de la batterie en mV
    int current_diff = 1000; // Seuil de différence de courant de la batterie en mA
    int max_charge_current;  // Seuil de courant de charge de la batterie en mA
    int reconnect_delay;     // Délai de reconnexion de la batterie en ms
    int Nb_switch_max;       // Nombre de commutations avant de déconnecter la batterie
    int Nb_switch[16];
    long reconnect_time[16];
    int max_discharge_current; // Ajouter cette ligne
};

struct AmpereHourTaskParams {
    int batteryIndex;
    float durationHours;
    int numSamples;
    BatteryManager* manager; // Ajouter un membre manager
};

/**
 * @class BatteryManager
 * @brief Classe pour gérer les batteries.
 */
class BatteryManager {
public:
    int getMaxVoltageBattery();
    int getMinVoltageBattery();
    float getAverageVoltage();
    float getVoltageCurrentRatio(int batteryIndex);
    void startAmpereHourConsumptionTask(int batteryIndex, float durationHours, int numSamples);
    static void ampereHourConsumptionTask(void* pvParameters);
    float getAmpereHourConsumption(int batteryIndex);

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

    bool isAmpereHourConsumptionTaskRunning(int batteryIndex);
    float getTotalConsumption();

private:
    float ampereHourConsumptions[16] = {0}; // Tableau pour stocker les consommations en ampère-heure
    bool ampereHourTaskRunning[16] = {false}; // Tableau pour suivre les tâches en cours
    float maxVoltage = 0;
    float minVoltage = FLT_MAX;
};

extern TCAHandler tcaHandler;
extern INAHandler inaHandler;
extern const bool print_message; // Ajouter cette ligne

#endif // BATT_PARALLELATOR_LIB_H