#ifndef KXKM_KXLOGGER_H
#define KXKM_KXLOGGER_H

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <cstddef>
#include <string>
using String = std::string;
#endif

#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif

class KxLogger {
public:
  enum DebugLevel {
    NONE,
    ERROR,
    WARNING,
    INFO,
    DEBUG,
    BATTERY,
    I2C,
    INFLUXDB,
    TIME,
    WIFI,
    SD,
    SPIFF,
    WEB
  };

  struct DebugLevelInfo {
    String name;
    bool enabled;
  };

  static void begin(const DebugLevelInfo levels[], size_t numLevels) {
    setDebugLevel(levels, numLevels);
  }

#ifdef ARDUINO
  static void begin(HardwareSerial *, unsigned long, const DebugLevelInfo levels[] = nullptr,
                    size_t numLevels = 0) {
    setDebugLevel(levels, numLevels);
  }
#endif

  static void setDebugLevel(const DebugLevelInfo levels[], size_t numLevels) {
    if (!levels || numLevels == 0) {
      return;
    }
    for (size_t i = 0; i < numLevels; ++i) {
      if (i < kMaxLevels) {
        levelEnabled()[i] = levels[i].enabled;
      }
    }
  }

  static bool isCategoryEnabled(DebugLevel level) {
    const int idx = static_cast<int>(level);
    if (idx < 0 || idx >= static_cast<int>(kMaxLevels)) {
      return false;
    }
    return levelEnabled()[idx];
  }

  static void print(DebugLevel level, const String &message) {
    log(level, message, false);
  }

  static void print(DebugLevel level, const char *message) {
    log(level, String(message ? message : ""), false);
  }

  static void println(DebugLevel level, const String &message) {
    log(level, message, true);
  }

  static void println(DebugLevel level, const char *message) {
    log(level, String(message ? message : ""), true);
  }

  static void enableCategory(DebugLevel level) {
    const int idx = static_cast<int>(level);
    if (idx >= 0 && idx < static_cast<int>(kMaxLevels)) {
      levelEnabled()[idx] = true;
    }
  }

  static void disableCategory(DebugLevel level) {
    const int idx = static_cast<int>(level);
    if (idx >= 0 && idx < static_cast<int>(kMaxLevels)) {
      levelEnabled()[idx] = false;
    }
  }

private:
  static constexpr size_t kMaxLevels = 13;

  static bool *levelEnabled() {
    static bool enabled[kMaxLevels] = {
        false, true, true, true, true, true, true, true, true, true, true, true, true};
    return enabled;
  }

  static const char *tag(DebugLevel level) {
    switch (level) {
    case ERROR:
      return "ERROR";
    case WARNING:
      return "WARN";
    case INFO:
      return "INFO";
    case DEBUG:
      return "DEBUG";
    case BATTERY:
      return "BATT";
    case I2C:
      return "I2C";
    case INFLUXDB:
      return "INFLUX";
    case TIME:
      return "TIME";
    case WIFI:
      return "WIFI";
    case SD:
      return "SD";
    case SPIFF:
      return "SPIFF";
    case WEB:
      return "WEB";
    default:
      return "LOG";
    }
  }

  static void log(DebugLevel level, const String &message, bool newline) {
    if (!isCategoryEnabled(level)) {
      return;
    }
#ifdef ESP_PLATFORM
    if (newline) {
      ESP_LOGI(tag(level), "%s", message.c_str());
    } else {
      ESP_LOGI(tag(level), "%s", message.c_str());
    }
#elif defined(ARDUINO)
    if (newline) {
      Serial.println(message);
    } else {
      Serial.print(message);
    }
#else
    (void)newline;
    (void)message;
#endif
  }
};

#endif
