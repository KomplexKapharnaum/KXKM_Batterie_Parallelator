#include "WebMutationRateLimit.h"

bool mutationRateLimitExceeded(MutationRateLimitSlot *slots, int slotCount,
                               uint32_t key, uint32_t nowMs,
                               uint8_t maxRequests,
                               uint32_t windowMs) {
  if (slots == nullptr || slotCount <= 0 || maxRequests == 0 || windowMs == 0) {
    return false;
  }

  int candidate = -1;
  for (int i = 0; i < slotCount; ++i) {
    if (slots[i].key == key) {
      candidate = i;
      break;
    }
    if (candidate < 0 && slots[i].key == 0) {
      candidate = i;
    }
  }

  if (candidate < 0) {
    candidate = 0;
  }

  MutationRateLimitSlot &slot = slots[candidate];
  const bool newWindow =
      (slot.key != key) || ((nowMs - slot.windowStartMs) > windowMs);

  if (newWindow) {
    slot.key = key;
    slot.windowStartMs = nowMs;
    slot.requestCount = 1;
    return false;
  }

  if (slot.requestCount >= maxRequests) {
    return true;
  }

  slot.requestCount++;
  return false;
}
