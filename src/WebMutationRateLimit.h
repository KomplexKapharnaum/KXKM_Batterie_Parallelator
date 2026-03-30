#ifndef WEB_MUTATION_RATE_LIMIT_H
#define WEB_MUTATION_RATE_LIMIT_H

#include <stdint.h>

struct MutationRateLimitSlot {
  uint32_t key = 0;
  uint32_t windowStartMs = 0;
  uint8_t requestCount = 0;
};

bool mutationRateLimitExceeded(MutationRateLimitSlot *slots, int slotCount,
                               uint32_t key, uint32_t nowMs,
                               uint8_t maxRequests,
                               uint32_t windowMs);

#endif // WEB_MUTATION_RATE_LIMIT_H
