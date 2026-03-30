#ifndef I2C_RECOVERY_POLICY_H
#define I2C_RECOVERY_POLICY_H

#include <stdint.h>

struct I2CRecoveryState {
  uint32_t consecutiveFailures;
  uint32_t threshold;
};

void i2cRecoveryInit(I2CRecoveryState &state, uint32_t threshold);
void i2cRecoveryRecordFailure(I2CRecoveryState &state);
void i2cRecoveryRecordSuccess(I2CRecoveryState &state);
bool i2cRecoveryShouldTrigger(const I2CRecoveryState &state);

#endif // I2C_RECOVERY_POLICY_H
