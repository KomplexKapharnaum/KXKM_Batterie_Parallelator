#include "I2CRecoveryPolicy.h"

void i2cRecoveryInit(I2CRecoveryState &state, uint32_t threshold) {
  state.consecutiveFailures = 0;
  state.threshold = threshold;
}

void i2cRecoveryRecordFailure(I2CRecoveryState &state) {
  state.consecutiveFailures++;
}

void i2cRecoveryRecordSuccess(I2CRecoveryState &state) {
  state.consecutiveFailures = 0;
}

bool i2cRecoveryShouldTrigger(const I2CRecoveryState &state) {
  if (state.threshold == 0) {
    return false;
  }
  return state.consecutiveFailures >= state.threshold;
}
