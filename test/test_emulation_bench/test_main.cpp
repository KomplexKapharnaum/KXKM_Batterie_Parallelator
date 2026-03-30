#include <cassert>

#include "../../src/I2CRecoveryPolicy.h"
#include "../mocks/MockI2CBus.h"

static void test_mock_i2c_nominal_reads_are_deterministic() {
  MockI2CBus bus;
  bus.setSample(0, 27.4f, 1.2f);

  float v = 0.0f;
  float i = 0.0f;

  assert(bus.readVoltage(0, v));
  assert(bus.readCurrent(0, i));
  assert(v == 27.4f);
  assert(i == 1.2f);
  assert(bus.getReadAttempts() == 2);
  assert(bus.getReadFailures() == 0);
}

static void test_fault_injection_and_recovery_threshold() {
  MockI2CBus bus;
  I2CRecoveryState recovery;
  i2cRecoveryInit(recovery, 5);

  bus.setSample(1, 25.9f, -0.4f);
  bus.injectFault(MockI2CFaultMode::Nack, 5);

  float v = 0.0f;
  for (int n = 0; n < 5; ++n) {
    const bool ok = bus.readVoltage(1, v);
    if (!ok) {
      i2cRecoveryRecordFailure(recovery);
    } else {
      i2cRecoveryRecordSuccess(recovery);
    }
  }

  assert(i2cRecoveryShouldTrigger(recovery));

  // Simulate post-recovery successful reads.
  bus.injectFault(MockI2CFaultMode::None, 0);
  assert(bus.readVoltage(1, v));
  i2cRecoveryRecordSuccess(recovery);
  assert(!i2cRecoveryShouldTrigger(recovery));
}

static void test_fault_budget_exhaustion_restores_reads() {
  MockI2CBus bus;
  bus.setSample(2, 26.3f, 0.7f);
  bus.injectFault(MockI2CFaultMode::Timeout, 2);

  float v = 0.0f;
  assert(!bus.readVoltage(2, v));
  assert(!bus.readVoltage(2, v));
  assert(bus.readVoltage(2, v));
  assert(v == 26.3f);
  assert(bus.getReadFailures() == 2);
}

static void test_invalid_channel_is_counted_as_failure() {
  MockI2CBus bus;
  float v = 0.0f;
  float i = 0.0f;

  assert(!bus.readVoltage(-1, v));
  assert(!bus.readCurrent(MockI2CBus::kMaxChannels, i));
  assert(bus.getReadAttempts() == 2);
  assert(bus.getReadFailures() == 2);
}

static void test_recovery_threshold_zero_never_triggers() {
  I2CRecoveryState recovery;
  i2cRecoveryInit(recovery, 0);

  for (int n = 0; n < 10; ++n) {
    i2cRecoveryRecordFailure(recovery);
  }

  assert(!i2cRecoveryShouldTrigger(recovery));
}

static void test_fault_mode_switching_keeps_latest_mode() {
  MockI2CBus bus;
  bus.injectFault(MockI2CFaultMode::StuckSda, 1);
  assert(bus.getFaultMode() == MockI2CFaultMode::StuckSda);

  bus.injectFault(MockI2CFaultMode::Nack, 1);
  assert(bus.getFaultMode() == MockI2CFaultMode::Nack);
}

int main() {
  test_mock_i2c_nominal_reads_are_deterministic();
  test_fault_injection_and_recovery_threshold();
  test_fault_budget_exhaustion_restores_reads();
  test_invalid_channel_is_counted_as_failure();
  test_recovery_threshold_zero_never_triggers();
  test_fault_mode_switching_keeps_latest_mode();
  return 0;
}
