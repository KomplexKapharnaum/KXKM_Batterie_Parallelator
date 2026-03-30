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
#include <KxLogger.h>
#include "INAHandler.h" // Inclure le fichier où inaHandler est déclaré
#include "BatteryParallelator.h" // Inclure l'en-tête où AmpereHourTaskParams est défini
#include <cmath>
#include <cstdio>
#include <new>

// Assurez-vous que `debugLogger` est déclaré et initialisé correctement
extern KxLogger debugLogger;
extern INAHandler inaHandler; // Déclarer inaHandler comme externe

bool BatteryManager::lockState(TickType_t timeout) {
    if (stateMutex == NULL) {
        return false;
    }
    return xSemaphoreTake(stateMutex, timeout) == pdTRUE;
}

float BatteryManager::getMaxVoltageBattery() {
    maxVoltage = 0; // Réinitialiser la tension maximale
    int maxVoltageBattery = -1;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {

        float voltage = inaHandler.read_volt(i);
        if (std::isnan(voltage)) continue; // Skip I2C errors
        if (voltage > maxVoltage) {
            maxVoltage = voltage;
            maxVoltageBattery = i;
        }
    }
    debugLogger.println(KxLogger::BATTERY, "Max voltage battery index: " + String(maxVoltageBattery));
    return maxVoltageBattery;
}

float BatteryManager::getMinVoltageBattery() { 
    minVoltage = 100.0; // Initialiser à une valeur élevée pour trouver le minimum
    int minVoltageBattery = -1;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        float voltage = inaHandler.read_volt(i);
        if (std::isnan(voltage)) continue; // Skip I2C errors
        if (voltage < minVoltage && voltage > 1) {
            minVoltage = voltage;
            minVoltageBattery = i;
        }
    }
    debugLogger.println(KxLogger::BATTERY, "Min voltage battery index: " + String(minVoltageBattery));
    return minVoltageBattery;
}

float BatteryManager::getAverageVoltage() {
    float totalVoltage = 0;
    int numBatteries = 0;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        float voltage = inaHandler.read_volt(i);
        if (std::isnan(voltage)) continue; // Skip I2C errors
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
        if (std::isnan(voltage)) continue; // Skip I2C errors
        if (voltage > maxVoltage) {
            maxVoltage = voltage;
        }
    }
    debugLogger.println(KxLogger::BATTERY, "Max voltage battery : " + String(maxVoltage));
    return maxVoltage;
}

float BatteryManager::getMinVoltage() {
    minVoltage = maxVoltage; // Réinitialiser la tension
    int batteryIndex = -1;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        float voltage = inaHandler.read_volt(i);
        if (std::isnan(voltage)) continue; // Skip I2C errors
        if (voltage < minVoltage && voltage > 1) {
            minVoltage = voltage;
            batteryIndex = i;
        }
    }
    if (batteryIndex != -1) {
        debugLogger.print(KxLogger::BATTERY, "Min voltage  : " + String(minVoltage));
        debugLogger.println(KxLogger::BATTERY, "v, battery index: " + String(batteryIndex));
    } else {
        debugLogger.println(KxLogger::BATTERY, "Min voltage  : " + String(minVoltage));
        debugLogger.println(KxLogger::BATTERY, "No valid battery found for min voltage.");
    }
    return minVoltage;
}

float BatteryManager::getVoltageCurrentRatio(int batteryIndex) {
    float voltage = inaHandler.read_volt(batteryIndex);
    float current = inaHandler.read_current(batteryIndex);
    if (std::isnan(voltage) || std::isnan(current)) return 0; // Skip I2C errors
    return current != 0 ? voltage / current : 0; // Éviter la division par zéro
}

void BatteryManager::startAmpereHourConsumptionTask(int batteryIndex, float durationHours, int numSamples) {
    if (batteryIndex < 0 || batteryIndex >= 16) {
        debugLogger.println(KxLogger::WARNING, "startAmpereHourConsumptionTask: battery index invalide");
        return;
    }
    if (durationHours <= 0.0f || numSamples <= 0) {
        debugLogger.println(KxLogger::WARNING, "startAmpereHourConsumptionTask: parametres invalides");
        return;
    }
    bool alreadyRunning = false;
    if (lockState()) {
        alreadyRunning = ampereHourTaskRunning[batteryIndex] &&
                         ampereHourTaskHandles[batteryIndex] != nullptr;
        xSemaphoreGive(stateMutex);
    }

    if (alreadyRunning) {
        debugLogger.println(KxLogger::INFO, "AmpereHour task deja active pour batterie " + String(batteryIndex));
        return;
    }

    AmpereHourTaskParams* params =
        new (std::nothrow) AmpereHourTaskParams{batteryIndex, durationHours, numSamples, this};
    if (params == nullptr) {
        debugLogger.println(KxLogger::ERROR, "startAmpereHourConsumptionTask: allocation params impossible");
        return;
    }

    char taskName[32];
    snprintf(taskName, sizeof(taskName), "AhTask_%d", batteryIndex);
    TaskHandle_t handle = nullptr;
    const BaseType_t created = xTaskCreate(ampereHourConsumptionTask, taskName, 4096, params, 1, &handle);
    if (created != pdPASS) {
        delete params;
        debugLogger.println(KxLogger::ERROR, "startAmpereHourConsumptionTask: echec creation task");
        return;
    }

    if (lockState()) {
        ampereHourTaskHandles[batteryIndex] = handle;
        ampereHourTaskRunning[batteryIndex] = true;
        xSemaphoreGive(stateMutex);
    } else {
        debugLogger.println(KxLogger::WARNING,
                            "startAmpereHourConsumptionTask: mutex indisponible");
    }
}

void BatteryManager::ampereHourConsumptionTask(void* pvParameters) {
    AmpereHourTaskParams* params = static_cast<AmpereHourTaskParams*>(pvParameters);
    const int batteryIndex = params->batteryIndex;
    const float durationHours = params->durationHours;
    const int numSamples = params->numSamples;
    BatteryManager* manager = static_cast<BatteryManager*>(params->manager);
    delete params;

    if (manager == nullptr || batteryIndex < 0 || batteryIndex >= 16 || durationHours <= 0.0f || numSamples <= 0) {
        vTaskDelete(nullptr);
        return;
    }

    float totalDischarge = 0.0f;
    float totalCharge = 0.0f;
    if (manager->lockState()) {
        totalDischarge = manager->ampereHourConsumptions[batteryIndex];
        totalCharge = manager->ampereHourCharges[batteryIndex];
        xSemaphoreGive(manager->stateMutex);
    }
    int sampleCount = 0;

    const float sampleIntervalMsFloat = (durationHours * 3600000.0f) / static_cast<float>(numSamples);
    uint32_t sampleIntervalMs = static_cast<uint32_t>(sampleIntervalMsFloat);
    if (sampleIntervalMs < 250U) sampleIntervalMs = 250U;
    if (sampleIntervalMs > 60000U) sampleIntervalMs = 60000U;
    const TickType_t sampleDelayTicks = pdMS_TO_TICKS(sampleIntervalMs);

    uint32_t lastSampleMs = millis();
    for (;;) {
        const uint32_t nowMs = millis();
        const uint32_t elapsedMs = nowMs - lastSampleMs;
        lastSampleMs = nowMs;
        const float elapsedHours = static_cast<float>(elapsedMs) / 3600000.0f;

        float current = inaHandler.read_current(batteryIndex);
        if (std::isnan(current)) {
            // I2C error — skip this sample, don't accumulate garbage
            vTaskDelay(sampleDelayTicks);
            continue;
        }

        if (elapsedHours > 0.0f) {
            if (current > 0.0f) { // Décharge
                totalDischarge += current * elapsedHours;
            } else if (current < 0.0f) { // Charge
                totalCharge += (-current) * elapsedHours;
            }
        }

        if (manager->lockState()) {
            manager->ampereHourConsumptions[batteryIndex] = totalDischarge;
            manager->ampereHourCharges[batteryIndex] = totalCharge;
            xSemaphoreGive(manager->stateMutex);
        }

        if (sampleCount % 100 == 0) {
            debugLogger.println(
                KxLogger::INFO,
                "Battery " + String(batteryIndex) +
                    " - Running Total Discharge: " + String(totalDischarge) + " Ah" +
                    ", Running Total Charge: " + String(totalCharge) + " Ah");
        }

        sampleCount++;
        vTaskDelay(sampleDelayTicks);
    }
}

float BatteryManager::getAmpereHourConsumption(int batteryIndex) {
    if (batteryIndex < 0 || batteryIndex >= 16) {
        return 0.0f;
    }

    float value = 0.0f;
    if (lockState()) {
        value = ampereHourConsumptions[batteryIndex];
        xSemaphoreGive(stateMutex);
    }

    debugLogger.println(KxLogger::BATTERY, "Getting Ampere Hour Consumption for battery " +
        String(batteryIndex + 1) + ": " + String(value) + " Ah");
    return value;
}

bool BatteryManager::isAmpereHourConsumptionTaskRunning(int batteryIndex) {
    if (batteryIndex < 0 || batteryIndex >= 16) {
        return false;
    }

    bool running = false;
    if (lockState()) {
        running = ampereHourTaskRunning[batteryIndex];
        xSemaphoreGive(stateMutex);
    }
    return running;
}

float BatteryManager::getTotalConsumption() {
    float totalConsumption = 0;
    const int nbIna = inaHandler.getNbINA();
    if (lockState()) {
        for (int i = 0; i < nbIna; i++) {
            totalConsumption += ampereHourConsumptions[i];
        }
        xSemaphoreGive(stateMutex);
    }
    return totalConsumption;
}

float BatteryManager::getAmpereHourCharge(int batteryIndex) {
    if (batteryIndex < 0 || batteryIndex >= 16) {
        return 0.0f;
    }

    float value = 0.0f;
    if (lockState()) {
        value = ampereHourCharges[batteryIndex];
        xSemaphoreGive(stateMutex);
    }
    return value;
}

float BatteryManager::getTotalCharge() {
    float totalCharge = 0;
    const int nbIna = inaHandler.getNbINA();
    if (lockState()) {
        for (int i = 0; i < nbIna; i++) {
            totalCharge += ampereHourCharges[i];
        }
        xSemaphoreGive(stateMutex);
    }
    return totalCharge;
}

float BatteryManager::getTotalCurrent() {
    float totalCurrent = 0;
    for (int i = 0; i < inaHandler.getNbINA(); i++) {
        float current = inaHandler.read_current(i);
        if (std::isnan(current)) continue; // Skip I2C errors
        totalCurrent += current;
    }
    return totalCurrent;
}
