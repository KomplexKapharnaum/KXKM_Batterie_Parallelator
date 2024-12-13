#ifndef BATT_PARALLELATOR_LIB_H
#define BATT_PARALLELATOR_LIB_H

#include "INA_NRJ_lib.h"
#include "TCA_NRJ_lib.h"
#include <cfloat> 


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
};

class BatteryManager {
public:
    int getMaxVoltageBattery();
    int getMinVoltageBattery();
    float getAverageVoltage();
    float getVoltageCurrentRatio(int batteryIndex);
    void startAmpereHourConsumptionTask(int batteryIndex, float durationHours, int numSamples);
    static void ampereHourConsumptionTask(void* pvParameters);
    float getAmpereHourConsumption(int batteryIndex);
    float getMaxVoltage();
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

BATTParallelator::BATTParallelator()
    : Nb_switch_max(5)
{
    memset(Nb_switch, 0, sizeof(Nb_switch));
    memset(reconnect_time, 0, sizeof(reconnect_time));
}

/**
 * @brief Définir le nombre de commutations avant de déconnecter la batterie.
 * 
 * @param nb Nombre de commutations.
 */
void BATTParallelator::set_nb_switch_on(int nb)
{
    Nb_switch_max = nb;
}

/**
 * @brief Définir le délai de reconnexion de la batterie en ms.
 * 
 * @param delay Délai de reconnexion.
 */
void BATTParallelator::set_reconnect_delay(int delay)
{
    reconnect_delay = delay;
}

/**
 * @brief Définir la tension maximale en mV.
 * 
 * @param voltage Tension maximale.
 */
void BATTParallelator::set_max_voltage(float voltage)
{
    max_voltage = voltage;
}

/**
 * @brief Définir la tension minimale en mV.
 * 
 * @param voltage Tension minimale.
 */
void BATTParallelator::set_min_voltage(float voltage)
{
    min_voltage = voltage;
}

/**
 * @brief Définir la différence de tension maximale en mV.
 * 
 * @param diff Différence de tension maximale.
 */
void BATTParallelator::set_max_diff_voltage(float diff)
{
    voltage_diff = diff;
}

/**
 * @brief Définir la différence de courant maximale en mA.
 * 
 * @param diff Différence de courant maximale.
 */
void BATTParallelator::set_max_diff_current(float diff)
{
    current_diff = diff;
}

/**
 * @brief Définir le courant maximal en mA.
 * 
 * @param current Courant maximal.
 */
void BATTParallelator::set_max_current(float current)
{
    max_current = current;
}

/**
 * @brief Définir le courant de charge maximal en mA.
 * 
 * @param current Courant de charge maximal.
 */
void BATTParallelator::set_max_charge_current(float current)
{
    max_charge_current = current;
}

/**
 * @brief Éteindre la batterie.
 * 
 * @param TCA_num Numéro TCA.
 * @param OUT_num Numéro de sortie.
 * @param switch_number Numéro de commutation.
 */
void BATTParallelator::switch_off_battery(int TCA_num, int OUT_num, int switch_number)
{
    tcaHandler.write(TCA_num, OUT_num, 0);         // switch off the battery
    tcaHandler.write(TCA_num, OUT_num * 2 + 8, 1); // set red led on
    tcaHandler.write(TCA_num, OUT_num * 2 + 9, 0); // set green led off
}

/**
 * @brief Allumer la batterie.
 * 
 * @param TCA_num Numéro TCA.
 * @param OUT_num Numéro de sortie.
 */
void BATTParallelator::switch_on_battery(int TCA_num, int OUT_num)
{
    tcaHandler.write(TCA_num, OUT_num, 1);         // switch on the battery
    tcaHandler.write(TCA_num, OUT_num * 2 + 8, 0); // set red led off
    tcaHandler.write(TCA_num, OUT_num * 2 + 9, 1); // set green led on
}

/**
 * @brief Obtenir le numéro TCA du numéro de dispositif INA correspondant au numéro de batterie.
 * 
 * @param INA_num Numéro INA.
 * @return uint8_t Numéro TCA.
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
 * @brief Vérifier l'état de la batterie et retourner true si les spécifications de la batterie sont respectées.
 * 
 * @param INA_num Numéro INA.
 * @return true Si les spécifications de la batterie sont respectées.
 * @return false Si les spécifications de la batterie ne sont pas respectées.
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
 * @brief Vérifier l'état de charge de la batterie et retourner true si la batterie est en charge.
 * 
 * @param INA_num Numéro INA.
 * @return true Si la batterie est en charge.
 * @return false Si la batterie n'est pas en charge.
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
 * @brief Allumer/éteindre la batterie.
 * 
 * @param INA_num Numéro INA.
 * @param switch_on Allumer ou éteindre.
 * @return true Si allumé.
 * @return false Si éteint.
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
 * @brief Vérifier l'état de la batterie et allumer/éteindre si nécessaire et possible (nombre maximal de commutations non atteint).
 * 
 * @param INA_num Numéro INA.
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
 * @brief Comparer la tension avec un seuil.
 * 
 * @param voltage Tension à comparer.
 * @param voltage_max Tension maximale.
 * @param diff Différence de tension.
 * @return float Résultat de la comparaison.
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
 * @brief Trouver la tension maximale dans un tableau de tensions.
 * 
 * @param battery_voltages Tableau de tensions de batterie.
 * @param num_batteries Nombre de batteries.
 * @return float Tension maximale.
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
 * @brief Trouver la tension minimale dans un tableau de tensions.
 * 
 * @param battery_voltages Tableau de tensions de batterie.
 * @param num_batteries Nombre de batteries.
 * @return float Tension minimale.
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
 * @brief Réinitialiser le compteur de commutations pour une batterie spécifique.
 * 
 * @param INA_num Numéro INA.
 */
void BATTParallelator::reset_switch_count(int INA_num)
{
    Nb_switch[INA_num] = 0;
    reconnect_time[INA_num] = 0;
}

/**
 * @brief Obtenir la tension maximale parmi toutes les batteries.
 * 
 * @return float Tension maximale.
 */
float BatteryManager::getMaxVoltage() {
    return maxVoltage;
}

/**
 * @brief Obtenir la tension minimale parmi toutes les batteries.
 * 
 * @return float Tension minimale.
 */
float BatteryManager::getMinVoltage() {
    return minVoltage;
}

/**
 * @brief Obtenir l'index de la batterie avec la tension maximale.
 * 
 * @return int Index de la batterie avec la tension maximale.
 */
int BatteryManager::getMaxVoltageBattery() {
    maxVoltage = 0; // Réinitialiser la tension maximale
    int maxVoltageBattery = -1;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        float voltage = inaHandler.read_volt(i);
        if (voltage > maxVoltage) {
            maxVoltage = voltage;
            maxVoltageBattery = i;
        }
    }
    return maxVoltageBattery;
}

/**
 * @brief Obtenir l'index de la batterie avec la tension minimale.
 * 
 * @return int Index de la batterie avec la tension minimale.
 */
int BatteryManager::getMinVoltageBattery() { 
    minVoltage = 0; // Réinitialiser la tension minimale
    int minVoltageBattery = -1;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        float voltage = inaHandler.read_volt(i);
        if (voltage < minVoltage) {
            minVoltage = voltage;
            minVoltageBattery = i;
        }
    }
    return minVoltageBattery;
}

/**
 * @brief Obtenir la tension moyenne de toutes les batteries.
 * 
 * @return float Tension moyenne.
 */
float BatteryManager::getAverageVoltage() {
    float totalVoltage = 0;
    int numBatteries = inaHandler.getNbINA();
    for (int i = 0; i < numBatteries; i++) {
        totalVoltage += inaHandler.read_volt(i);
    }
    return totalVoltage / numBatteries;
}

/**
 * @brief Obtenir le rapport tension/courant pour une batterie spécifique.
 * 
 * @param batteryIndex Index de la batterie.
 * @return float Rapport tension/courant.
 */
float BatteryManager::getVoltageCurrentRatio(int batteryIndex) {
    float voltage = inaHandler.read_volt(batteryIndex);
    float current = inaHandler.read_current(batteryIndex);
    if (current != 0) {
        return voltage / current;
    } else {
        return 0; // Avoid division by zero
    }
}

struct AmpereHourTaskParams {
    int batteryIndex;
    float durationHours;
    int numSamples;
    BatteryManager* manager; // Ajouter un membre manager
};

/**
 * @brief Démarrer la tâche de consommation en ampère-heure pour une batterie spécifique.
 * 
 * @param batteryIndex Index de la batterie.
 * @param durationHours Durée de la tâche en heures.
 * @param numSamples Nombre d'échantillons à prendre.
 */
void BatteryManager::startAmpereHourConsumptionTask(int batteryIndex, float durationHours, int numSamples) {
    AmpereHourTaskParams* params = new AmpereHourTaskParams{batteryIndex, durationHours, numSamples, this}; // Passer this comme manager
    ampereHourTaskRunning[batteryIndex] = true; // Marquer la tâche comme en cours
    xTaskCreate(ampereHourConsumptionTask, "AmpereHourConsumptionTask", 2048, params, 1, NULL);
}

/**
 * @brief Tâche pour mesurer la consommation en ampère-heure.
 * 
 * @param pvParameters Paramètres pour la tâche.
 */
void BatteryManager::ampereHourConsumptionTask(void* pvParameters) {
    AmpereHourTaskParams* params = static_cast<AmpereHourTaskParams*>(pvParameters);
    int batteryIndex = params->batteryIndex;
    float durationHours = params->durationHours;
    int numSamples = params->numSamples;

    float totalCurrent = 0;
    float sampleInterval = (durationHours * 3600000) / numSamples; // Convertir les heures en millisecondes
    unsigned long startTime = millis();
    for (int i = 0; i < numSamples; i++) {
        totalCurrent += inaHandler.read_current(batteryIndex); // Lire le courant de la batterie
        while (millis() - startTime < sampleInterval) {
            vTaskDelay(1); // Céder aux autres tâches
        }
        startTime += sampleInterval;
    }
    float ampereHourConsumption = (totalCurrent / numSamples) * durationHours; // Calculer la consommation en ampère-heure
    Serial.print("Ampere-hour consumption: ");
    Serial.println(ampereHourConsumption);

    BatteryManager* manager = static_cast<BatteryManager*>(params->manager);
    manager->ampereHourConsumptions[batteryIndex] = ampereHourConsumption; // Stocker la consommation en ampère-heure
    manager->ampereHourTaskRunning[batteryIndex] = false; // Marquer la tâche comme terminée

    delete params;
    vTaskDelete(NULL); // Supprimer la tâche
}

/**
 * @brief Obtenir la consommation en ampère-heure pour une batterie spécifique.
 * 
 * @param batteryIndex Index de la batterie.
 * @return float Consommation en ampère-heure.
 */
float BatteryManager::getAmpereHourConsumption(int batteryIndex) {
    return ampereHourConsumptions[batteryIndex]; // Retourner la consommation en ampère-heure pour la batterie spécifiée
}

/**
 * @brief Vérifier si une tâche de consommation en ampère-heure est en cours pour une batterie spécifique.
 * 
 * @param batteryIndex Index de la batterie.
 * @return bool True si une tâche est en cours, false sinon.
 */
bool BatteryManager::isAmpereHourConsumptionTaskRunning(int batteryIndex) {
    return ampereHourTaskRunning[batteryIndex];
}

/**
 * @brief Obtenir la consommation totale en ampère-heure de toutes les batteries.
 * 
 * @return float Consommation totale en ampère-heure.
 */
float BatteryManager::getTotalConsumption() {
    float totalConsumption = 0;
    for (int i = 0; i <= inaHandler.getNbINA(); i++) {
        totalConsumption += ampereHourConsumptions[i];
    }
    return totalConsumption;
}

#endif // BATT_PARALLELATOR_LIB_H