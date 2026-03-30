#ifndef MOCK_I2C_BUS_H
#define MOCK_I2C_BUS_H

#include <stddef.h>
#include <stdint.h>

struct MockInaSample {
  float voltage;
  float current;
};

enum class MockI2CFaultMode : uint8_t {
  None = 0,
  Nack,
  Timeout,
  StuckSda,
};

class MockI2CBus {
public:
  static constexpr int kMaxChannels = 16;

  MockI2CBus() { reset(); }

  void reset() {
    faultMode = MockI2CFaultMode::None;
    faultReadBudget = 0;
    readAttempts = 0;
    readFailures = 0;
    for (int i = 0; i < kMaxChannels; ++i) {
      samples[i].voltage = 0.0f;
      samples[i].current = 0.0f;
    }
  }

  void setSample(int channel, float voltage, float current) {
    if (channel < 0 || channel >= kMaxChannels) {
      return;
    }
    samples[channel].voltage = voltage;
    samples[channel].current = current;
  }

  void injectFault(MockI2CFaultMode mode, int readBudget) {
    faultMode = mode;
    faultReadBudget = (readBudget < 0) ? 0 : readBudget;
  }

  bool readVoltage(int channel, float &outVoltage) {
    readAttempts++;
    if (!isValidChannel(channel) || isFaultActive()) {
      readFailures++;
      return false;
    }
    outVoltage = samples[channel].voltage;
    return true;
  }

  bool readCurrent(int channel, float &outCurrent) {
    readAttempts++;
    if (!isValidChannel(channel) || isFaultActive()) {
      readFailures++;
      return false;
    }
    outCurrent = samples[channel].current;
    return true;
  }

  size_t getReadAttempts() const { return readAttempts; }
  size_t getReadFailures() const { return readFailures; }
  MockI2CFaultMode getFaultMode() const { return faultMode; }

private:
  MockInaSample samples[kMaxChannels];
  MockI2CFaultMode faultMode = MockI2CFaultMode::None;
  int faultReadBudget = 0;
  size_t readAttempts = 0;
  size_t readFailures = 0;

  bool isValidChannel(int channel) const {
    return channel >= 0 && channel < kMaxChannels;
  }

  bool isFaultActive() {
    if (faultMode == MockI2CFaultMode::None) {
      return false;
    }
    if (faultReadBudget == 0) {
      return false;
    }
    faultReadBudget--;
    return true;
  }
};

#endif // MOCK_I2C_BUS_H
