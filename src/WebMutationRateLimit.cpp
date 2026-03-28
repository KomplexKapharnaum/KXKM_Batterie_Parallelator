#include "WebMutationRateLimit.h"

void initMutationRateLimitSlots(MutationRateLimitSlot *slots, int slotCount) {
  if (slots == nullptr || slotCount <= 0) {
    return;
  }

  for (int i = 0; i < slotCount; ++i) {
    slots[i].key = 0;
    slots[i].windowStartMs = 0;
    slots[i].requestCount = 0;
  }
}

bool allowMutationRequest(MutationRateLimitSlot *slots, int slotCount,
                          uint32_t key, uint32_t nowMs,
                          uint32_t windowMs, uint8_t maxRequests) {
  if (slots == nullptr || slotCount <= 0 || key == 0 || maxRequests == 0) {
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
  const bool resetWindow =
      (slot.key != key) || ((nowMs - slot.windowStartMs) > windowMs);

  if (resetWindow) {
    slot.key = key;
    slot.windowStartMs = nowMs;
    slot.requestCount = 1;
    return true;
  }

  if (slot.requestCount >= maxRequests) {
    return false;
  }

  slot.requestCount++;
  return true;
}
