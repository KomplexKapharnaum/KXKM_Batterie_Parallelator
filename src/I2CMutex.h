#ifndef I2C_MUTEX_H
#define I2C_MUTEX_H

#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "pin_mapppings.h"

/**
 * @brief Global I2C bus mutex for thread-safe access.
 *
 * ESP32 Wire library is NOT thread-safe. All I2C operations
 * (INA reads, TCA reads/writes) must be serialized via this mutex.
 * Created in main.cpp setup(), used by INAHandler and TCAHandler.
 */
extern SemaphoreHandle_t i2cMutex;
extern volatile uint32_t g_i2cConsecutiveFailures;

static constexpr uint32_t kI2CRecoveryThreshold = 5;

inline void i2cRecordFailure() {
  g_i2cConsecutiveFailures++;
}

inline void i2cResetFailureCounter() {
  g_i2cConsecutiveFailures = 0;
}

inline bool i2cShouldRecover() {
  return g_i2cConsecutiveFailures >= kI2CRecoveryThreshold;
}

/**
 * @brief Initialize the I2C mutex. Must be called before any task creation.
 */
inline void i2cMutexInit() {
  i2cMutex = xSemaphoreCreateMutex();
  configASSERT(i2cMutex != NULL);
}

/**
 * @brief RAII-style I2C lock guard for C++ scope-based locking.
 */
class I2CLockGuard {
public:
  explicit I2CLockGuard(TickType_t timeout = pdMS_TO_TICKS(100))
      : acquired(false) {
    if (i2cMutex != NULL) {
      acquired = (xSemaphoreTake(i2cMutex, timeout) == pdTRUE);
    }
  }
  ~I2CLockGuard() {
    if (acquired && i2cMutex != NULL) {
      xSemaphoreGive(i2cMutex);
    }
  }
  bool isAcquired() const { return acquired; }

  // Non-copyable
  I2CLockGuard(const I2CLockGuard &) = delete;
  I2CLockGuard &operator=(const I2CLockGuard &) = delete;

private:
  bool acquired;
};

/**
 * @brief Recover a stuck I2C bus by clocking SCL until SDA is released.
 * Call this when I2C transactions consistently fail (NACK/timeout).
 * Must be called with the I2C mutex held.
 */
inline void i2cBusRecovery() {
  Wire.end();
  pinMode(SDA_pin, INPUT_PULLUP);
  pinMode(SCL_pin, OUTPUT);
  // Clock SCL up to 9 times to release a stuck slave
  for (int i = 0; i < 9; i++) {
    digitalWrite(SCL_pin, LOW);
    delayMicroseconds(5);
    digitalWrite(SCL_pin, HIGH);
    delayMicroseconds(5);
    if (digitalRead(SDA_pin) == HIGH) break; // SDA released
  }
  // Re-initialize Wire
  Wire.begin(SDA_pin, SCL_pin);
}

#endif // I2C_MUTEX_H
