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
 * @file INAHandler.cpp
 * @brief Implémentation de la classe INAHandler pour gérer les appareils INA.
 * @details Backend migré vers RobTillaart/INA226.
 */

#include "INAHandler.h"
#include "I2CMutex.h"
#include <KxLogger.h>
#include <cmath>
#include <cstring>
#include <new>

extern KxLogger debugLogger;
volatile uint32_t g_i2cConsecutiveFailures = 0;

static void tryI2CRecoveryIfNeeded(const char *origin) {
    if (!i2cShouldRecover()) {
        return;
    }

    I2CLockGuard recoveryLock(pdMS_TO_TICKS(20));
    if (!recoveryLock.isAcquired()) {
        return;
    }

    i2cBusRecovery();
    i2cResetFailureCounter();
    debugLogger.println(KxLogger::WARNING,
                        String("I2C recovery executed from ") + origin);
}

INAHandler::INAHandler()
    : deviceNumber(UINT8_MAX), Nb_INA(0)
{
    std::memset((void *)sumBusMillVolts, 0, sizeof(sumBusMillVolts));
    std::memset((void *)sumBusMicroAmp, 0, sizeof(sumBusMicroAmp));
    std::memset((void *)readings, 0, sizeof(readings));
    std::memset((void *)INA_address_connected, 0, sizeof(INA_address_connected));
    for (int i = 0; i < 16; i++) {
        sensors[i] = nullptr;
    }
}

INAHandler::~INAHandler() {
    for (int i = 0; i < 16; i++) {
        delete sensors[i];
        sensors[i] = nullptr;
    }
}

bool INAHandler::isValidIndex(uint8_t sensorIndex) const {
    return sensorIndex < Nb_INA && sensors[sensorIndex] != nullptr;
}

void INAHandler::begin(const uint8_t amp, const uint16_t micro_ohm)
{
    for (int i = 0; i < 16; i++) {
        delete sensors[i];
        sensors[i] = nullptr;
        INA_address_connected[i] = 0;
    }
    Nb_INA = 0;
    deviceNumber = UINT8_MAX;

    const float shuntOhm = static_cast<float>(micro_ohm) / 1000000.0f;
    const float maxCurrentA = static_cast<float>(amp);

    for (uint8_t i = 0; i < 16; i++) {
        INA226* sensor = new (std::nothrow) INA226(static_cast<uint8_t>(INA_ADDR[i]), &Wire);
        if (sensor == nullptr) {
            debugLogger.println(KxLogger::ERROR, "INAHandler: allocation INA226 impossible");
            continue;
        }

        if (!sensor->begin()) {
            delete sensor;
            continue;
        }

        sensors[Nb_INA] = sensor;
        INA_address_connected[Nb_INA] = static_cast<byte>(INA_ADDR[i]);
        initialize_ina(Nb_INA, maxCurrentA, shuntOhm);

        if (deviceNumber == UINT8_MAX) {
            deviceNumber = Nb_INA;
        }

        debugLogger.println(KxLogger::INFO,
                            "Trouvé INA226 à l'adresse " + String(INA_ADDR[i]) +
                                " (slot " + String(Nb_INA) + ")");
        Nb_INA++;
    }

    if (Nb_INA == 0) {
        debugLogger.println(KxLogger::ERROR, "Aucun INA226 détecté.");
        return;
    }

    Wire.setClock(static_cast<uint32_t>(i2cSpeedKHz) * 1000U);
}

void INAHandler::initialize_ina(const uint8_t sensorIndex, float amp, float shunt_ohm)
{
    if (!isValidIndex(sensorIndex)) {
        return;
    }

    const int calibState = sensors[sensorIndex]->setMaxCurrentShunt(amp, shunt_ohm, true);
    if (calibState != 0) {
        debugLogger.println(KxLogger::WARNING,
                            "INA226 calibration state=" + String(calibState) +
                                " pour slot " + String(sensorIndex));
    }
}

void INAHandler::read(const uint8_t sensorIndex)
{
    if (!isValidIndex(sensorIndex)) {
        return;
    }

    I2CLockGuard lock;
    if (!lock.isAcquired()) {
        i2cRecordFailure();
        tryI2CRecoveryIfNeeded("INA.read");
        return;
    }

    const float busV = sensors[sensorIndex]->getBusVoltage();
    if (sensors[sensorIndex]->getLastError() != 0) {
        i2cRecordFailure();
        if (i2cShouldRecover()) {
            i2cBusRecovery();
            i2cResetFailureCounter();
            debugLogger.println(KxLogger::WARNING,
                "I2C recovery executed from INA.read sensor error");
        }
        debugLogger.println(KxLogger::WARNING,
            "INA226 I2C error on read() slot " + String(sensorIndex));
        return;
    }
    i2cResetFailureCounter();
    const float currentA = sensors[sensorIndex]->getCurrent();
    const float powerW = sensors[sensorIndex]->getPower();
    (void)busV;
    (void)currentA;
    (void)powerW;
}

float INAHandler::read_current(const uint8_t sensorIndex)
{
    if (!isValidIndex(sensorIndex)) return NAN;
    I2CLockGuard lock;
    if (!lock.isAcquired()) {
        i2cRecordFailure();
        tryI2CRecoveryIfNeeded("INA.read_current");
        return NAN;
    }
    float val = sensors[sensorIndex]->getCurrent();
    if (sensors[sensorIndex]->getLastError() != 0) {
        i2cRecordFailure();
        if (i2cShouldRecover()) {
            i2cBusRecovery();
            i2cResetFailureCounter();
            debugLogger.println(KxLogger::WARNING,
                "I2C recovery executed from INA.read_current sensor error");
        }
        debugLogger.println(KxLogger::WARNING,
            "INA226 I2C error on read_current() slot " + String(sensorIndex));
        return NAN;
    }
    i2cResetFailureCounter();
    return val;
}

float INAHandler::read_volt(const uint8_t sensorIndex)
{
    if (!isValidIndex(sensorIndex)) return NAN;
    I2CLockGuard lock;
    if (!lock.isAcquired()) {
        i2cRecordFailure();
        tryI2CRecoveryIfNeeded("INA.read_volt");
        return NAN;
    }
    float val = sensors[sensorIndex]->getBusVoltage();
    if (sensors[sensorIndex]->getLastError() != 0) {
        i2cRecordFailure();
        if (i2cShouldRecover()) {
            i2cBusRecovery();
            i2cResetFailureCounter();
            debugLogger.println(KxLogger::WARNING,
                "I2C recovery executed from INA.read_volt sensor error");
        }
        debugLogger.println(KxLogger::WARNING,
            "INA226 I2C error on read_volt() slot " + String(sensorIndex));
        return NAN;
    }
    i2cResetFailureCounter();
    return val;
}

float INAHandler::read_power(const uint8_t sensorIndex)
{
    if (!isValidIndex(sensorIndex)) return NAN;
    I2CLockGuard lock;
    if (!lock.isAcquired()) {
        i2cRecordFailure();
        tryI2CRecoveryIfNeeded("INA.read_power");
        return NAN;
    }
    float val = sensors[sensorIndex]->getPower();
    if (sensors[sensorIndex]->getLastError() != 0) {
        i2cRecordFailure();
        if (i2cShouldRecover()) {
            i2cBusRecovery();
            i2cResetFailureCounter();
            debugLogger.println(KxLogger::WARNING,
                "I2C recovery executed from INA.read_power sensor error");
        }
        debugLogger.println(KxLogger::WARNING,
            "INA226 I2C error on read_power() slot " + String(sensorIndex));
        return NAN;
    }
    i2cResetFailureCounter();
    return val;
}

bool INAHandler::read_voltage_current(const uint8_t sensorIndex, float &voltage, float &current)
{
    voltage = NAN;
    current = NAN;
    if (!isValidIndex(sensorIndex)) return false;

    I2CLockGuard lock;
    if (!lock.isAcquired()) {
        i2cRecordFailure();
        tryI2CRecoveryIfNeeded("INA.read_voltage_current");
        return false;
    }

    voltage = sensors[sensorIndex]->getBusVoltage();
    if (sensors[sensorIndex]->getLastError() != 0) {
        i2cRecordFailure();
        if (i2cShouldRecover()) {
            i2cBusRecovery();
            i2cResetFailureCounter();
            debugLogger.println(KxLogger::WARNING,
                "I2C recovery executed from INA.read_voltage_current voltage error");
        }
        debugLogger.println(KxLogger::WARNING,
            "INA226 I2C error on read_voltage_current() voltage slot " + String(sensorIndex));
        return false;
    }

    current = sensors[sensorIndex]->getCurrent();
    if (sensors[sensorIndex]->getLastError() != 0) {
        i2cRecordFailure();
        if (i2cShouldRecover()) {
            i2cBusRecovery();
            i2cResetFailureCounter();
            debugLogger.println(KxLogger::WARNING,
                "I2C recovery executed from INA.read_voltage_current current error");
        }
        debugLogger.println(KxLogger::WARNING,
            "INA226 I2C error on read_voltage_current() current slot " + String(sensorIndex));
        return false;
    }

    i2cResetFailureCounter();
    return true;
}

uint8_t INAHandler::getDeviceAddress(const uint8_t sensorIndex)
{
    if (sensorIndex < Nb_INA) {
        return INA_address_connected[sensorIndex];
    }
    return 0;
}

uint8_t INAHandler::getNbINA()
{
    return Nb_INA;
}

void INAHandler::set_max_voltage(const float_t voltage)
{
    max_voltage = static_cast<int>(voltage);
}

void INAHandler::set_min_voltage(const float_t voltage)
{
    min_voltage = static_cast<int>(voltage);
}

void INAHandler::set_max_current(const float_t current)
{
    max_current = static_cast<int>(current);
}

void INAHandler::set_max_charge_current(const float_t current)
{
    max_charge_current = static_cast<int>(current);
}

void INAHandler::setI2CSpeed(int speed)
{
    i2cSpeedKHz = speed;
    Wire.setClock(static_cast<uint32_t>(i2cSpeedKHz) * 1000U);
}

int INAHandler::detect_batteries()
{
    return static_cast<int>(Nb_INA);
}
