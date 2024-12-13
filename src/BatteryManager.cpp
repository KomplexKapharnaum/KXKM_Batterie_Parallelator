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
 * @file BatteryManager.cpp
 * @brief Implémentation de la classe BatteryManager pour gérer les batteries.
 * @details Créé par Clément Saillant pour Komplex Kapharnum assisté par IA.
    Calcul et autres fonctions par Nicolas Guichard
    Modification de la classe BatteryManager pour ajouter une tâche de consommation en ampère-heure pour chaque batterie.
 */

#include "Batt_Parallelator_lib.h"

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

float BatteryManager::getAverageVoltage() {
    float totalVoltage = 0;
    int numBatteries = inaHandler.getNbINA();
    for (int i = 0; i < numBatteries; i++) {
        totalVoltage += inaHandler.read_volt(i);
    }
    return totalVoltage / numBatteries;
}

float BatteryManager::getMaxVoltage() {
    return maxVoltage;
}

float BatteryManager::getMinVoltage() {
    return minVoltage;
}

float BatteryManager::getVoltageCurrentRatio(int batteryIndex) {
    float voltage = inaHandler.read_volt(batteryIndex);
    float current = inaHandler.read_current(batteryIndex);
    if (current != 0) {
        return voltage / current;
    } else {
        return 0; // Avoid division by zero
    }
}

void BatteryManager::startAmpereHourConsumptionTask(int batteryIndex, float durationHours, int numSamples) {
    AmpereHourTaskParams* params = new AmpereHourTaskParams{batteryIndex, durationHours, numSamples, this}; // Passer this comme manager
    ampereHourTaskRunning[batteryIndex] = true; // Marquer la tâche comme en cours
    xTaskCreate(ampereHourConsumptionTask, "AmpereHourConsumptionTask", 2048, params, 1, NULL);
}

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

float BatteryManager::getAmpereHourConsumption(int batteryIndex) {
    return ampereHourConsumptions[batteryIndex]; // Retourner la consommation en ampère-heure pour la batterie spécifiée
}

bool BatteryManager::isAmpereHourConsumptionTaskRunning(int batteryIndex) {
    return ampereHourTaskRunning[batteryIndex];
}

float BatteryManager::getTotalConsumption() {
    float totalConsumption = 0;
    for (int i = 0; i <= inaHandler.getNbINA(); i++) {
        totalConsumption += ampereHourConsumptions[i];
    }
    return totalConsumption;
}
