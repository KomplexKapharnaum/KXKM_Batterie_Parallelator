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

#include "BatteryManager.h" // Remplacer l'ancien include par le nouveau
#include <DebugLogger.h>
#include "INA_NRJ_lib.h" // Inclure le fichier où inaHandler est déclaré
#include "BatteryParallelator.h" // Inclure l'en-tête où AmpereHourTaskParams est défini

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern DebugLogger debugLogger;
extern INAHandler inaHandler; // Déclarer inaHandler comme externe

float BatteryManager::getMaxVoltageBattery() {
    maxVoltage = 0; // Réinitialiser la tension maximale
    int maxVoltageBattery = -1;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        
        float voltage = inaHandler.read_volt(i);
        if (voltage > maxVoltage) {
            maxVoltage = voltage;
            maxVoltageBattery = i;
        }
    }
    debugLogger.println(DebugLogger::BATTERY, "Max voltage battery index: " + String(maxVoltageBattery));
    return maxVoltageBattery;
}

float BatteryManager::getMinVoltageBattery() { 
    minVoltage = 100.0; // Initialiser à une valeur élevée pour trouver le minimum
    int minVoltageBattery = -1;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        float voltage = inaHandler.read_volt(i);
        if (voltage < minVoltage && voltage > 1) {
            minVoltage = voltage;
            minVoltageBattery = i;
        }
    }
    debugLogger.println(DebugLogger::BATTERY, "Min voltage battery index: " + String(minVoltageBattery));
    return minVoltageBattery;
}

float BatteryManager::getAverageVoltage() {
    float totalVoltage = 0;
    int numBatteries = 0;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        float voltage = inaHandler.read_volt(i);
        if (voltage > 1) {
            totalVoltage += voltage;
            numBatteries++;
        }
    }
    return numBatteries > 0 ? totalVoltage / numBatteries : 0;
}

float BatteryManager::getMaxVoltage() {
    maxVoltage = 0; // Réinitialiser la tension maximale
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        float voltage = inaHandler.read_volt(i);
        if (voltage > maxVoltage) {
            maxVoltage = voltage;
        }
    }
    debugLogger.println(DebugLogger::BATTERY, "Max voltage battery : " + String(maxVoltage));
    return maxVoltage;
}

float BatteryManager::getMinVoltage() {
    minVoltage = maxVoltage; // Réinitialiser la tension
    int batteryIndex = -1;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        float voltage = inaHandler.read_volt(i);
        if (voltage < minVoltage && voltage > 1) {
            minVoltage = voltage;
            batteryIndex = i;
        }
    }
    if (batteryIndex != -1) {
        debugLogger.print(DebugLogger::BATTERY, "Min voltage  : " + String(minVoltage));
        debugLogger.println(DebugLogger::BATTERY, "v, battery index: " + String(batteryIndex));
    } else {
        debugLogger.println(DebugLogger::BATTERY, "Min voltage  : " + String(minVoltage));
        debugLogger.println(DebugLogger::BATTERY, "No valid battery found for min voltage.");
    }
    return minVoltage;
}

float BatteryManager::getVoltageCurrentRatio(int batteryIndex) {
    float voltage = inaHandler.read_volt(batteryIndex);
    float current = inaHandler.read_current(batteryIndex);
    return current != 0 ? voltage / current : 0; // Éviter la division par zéro
}

void BatteryManager::startAmpereHourConsumptionTask(int batteryIndex, float durationHours, int numSamples) {
    AmpereHourTaskParams* params = new AmpereHourTaskParams{batteryIndex, durationHours, numSamples, this}; // Passer this comme manager
    ampereHourTaskRunning[batteryIndex] = true; // Marquer la tâche comme en cours
    xTaskCreate(ampereHourConsumptionTask, "AmpereHourConsumptionTask", 8192, params, 1, NULL);
}

void BatteryManager::ampereHourConsumptionTask(void* pvParameters) {
    AmpereHourTaskParams* params = static_cast<AmpereHourTaskParams*>(pvParameters);
    int batteryIndex = params->batteryIndex;
    float durationHours = params->durationHours;
    int numSamples = params->numSamples;
    BatteryManager* manager = static_cast<BatteryManager*>(params->manager);

    // On peut libérer la mémoire des paramètres une fois qu'ils sont extraits
    delete params;

    float totalDischarge = 0;  // Courant positif (décharge)
    float totalCharge = 0;     // Courant négatif (charge)
    int dischargeCount = 0;    // Nombre d'échantillons de décharge
    int chargeCount = 0;       // Nombre d'échantillons de charge
    
    float sampleInterval = (durationHours * 3600000) / numSamples; // Convertir les heures en millisecondes
    unsigned long startTime = millis();
    int sampleCount = 0;
    
    // Fonctionnement en continu - boucle infinie
    while (true) {
        float current = inaHandler.read_current(batteryIndex);
        
        // Décharge et charge sont des ampères-heures accumulés
        // On convertit le courant (A) en ampères-heures en divisant par le nombre d'échantillons par heure
        float ampereHourPerSample = durationHours / numSamples;
        
        if (current > 0) {  // Décharge
            totalDischarge += current * ampereHourPerSample;
            dischargeCount++;
        } else if (current < 0) {  // Charge
            totalCharge += -current * ampereHourPerSample;  // Valeur absolue
            chargeCount++;
        }
        
        
        // Mise à jour des valeurs d'ampères-heures dans le gestionnaire de batterie
        manager->ampereHourConsumptions[batteryIndex] = totalDischarge;
        manager->ampereHourCharges[batteryIndex] = totalCharge;
        
        // Loguer périodiquement les totaux
        if (sampleCount % 100 == 0) {
            debugLogger.println(DebugLogger::INFO, "Battery " + String(batteryIndex) + 
                " - Running Total Discharge: " + String(totalDischarge) + " Ah (" + String(dischargeCount) + " samples)" +
                ", Running Total Charge: " + String(totalCharge) + " Ah (" + String(chargeCount) + " samples)");
        }
        
        sampleCount++;
        
        // Attendre l'intervalle d'échantillonnage
        vTaskDelay(pdMS_TO_TICKS(sampleInterval));
    }
    
    // Ce code n'est plus atteignable avec la boucle infinie
    // vTaskDelete(NULL);
}

float BatteryManager::getAmpereHourConsumption(int batteryIndex) {
    debugLogger.println(DebugLogger::BATTERY, "Getting Ampere Hour Consumption for battery " + 
        String(batteryIndex+1) + ": " + String(ampereHourConsumptions[batteryIndex]) + " Ah");
    return ampereHourConsumptions[batteryIndex];
}

bool BatteryManager::isAmpereHourConsumptionTaskRunning(int batteryIndex) {
    return ampereHourTaskRunning[batteryIndex];
}

float BatteryManager::getTotalConsumption() {
    float totalConsumption = 0;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        totalConsumption += ampereHourConsumptions[i];
    }
    return totalConsumption;
}

float BatteryManager::getAmpereHourCharge(int batteryIndex) {
    return ampereHourCharges[batteryIndex];
}

float BatteryManager::getTotalCharge() {
    float totalCharge = 0;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        totalCharge += ampereHourCharges[i];
    }
    return totalCharge;
}

float BatteryManager::getTotalCurrent() {
    float totalCurrent = 0;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        totalCurrent += inaHandler.read_current(i);
    }
    return totalCurrent;
}
