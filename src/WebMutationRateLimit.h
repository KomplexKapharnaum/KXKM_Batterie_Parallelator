#ifndef WEB_MUTATION_RATE_LIMIT_H
#define WEB_MUTATION_RATE_LIMIT_H

#include <stdint.h>

struct MutationRateLimitSlot {
  uint32_t key;
  uint32_t windowStartMs;
  uint8_t requestCount;
};

void initMutationRateLimitSlots(MutationRateLimitSlot *slots, int slotCount);
bool allowMutationRequest(MutationRateLimitSlot *slots, int slotCount,
                          uint32_t key, uint32_t nowMs,
                          uint32_t windowMs, uint8_t maxRequests);

#endif // WEB_MUTATION_RATE_LIMIT_H
