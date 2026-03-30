#include <cassert>

#include "../../src/WebMutationRateLimit.h"

static void test_allows_until_limit_then_blocks() {
  MutationRateLimitSlot slots[2] = {};
  const uint32_t key = 0x01020304;

  assert(!mutationRateLimitExceeded(slots, 2, key, 1000, 3, 10000));
  assert(!mutationRateLimitExceeded(slots, 2, key, 1001, 3, 10000));
  assert(!mutationRateLimitExceeded(slots, 2, key, 1002, 3, 10000));
  assert(mutationRateLimitExceeded(slots, 2, key, 1003, 3, 10000));
}

static void test_resets_after_window() {
  MutationRateLimitSlot slots[1] = {};
  const uint32_t key = 0x01020304;

  assert(!mutationRateLimitExceeded(slots, 1, key, 1000, 2, 100));
  assert(!mutationRateLimitExceeded(slots, 1, key, 1001, 2, 100));
  assert(mutationRateLimitExceeded(slots, 1, key, 1002, 2, 100));

  // Strictly greater than window resets per runtime behavior
  assert(!mutationRateLimitExceeded(slots, 1, key, 1102, 2, 100));
}

static void test_different_keys_use_different_slots() {
  MutationRateLimitSlot slots[2] = {};

  assert(!mutationRateLimitExceeded(slots, 2, 0x11111111, 1000, 1, 1000));
  assert(!mutationRateLimitExceeded(slots, 2, 0x22222222, 1001, 1, 1000));

  // Now each key has reached maxRequests=1; next request should block both
  assert(mutationRateLimitExceeded(slots, 2, 0x11111111, 1002, 1, 1000));
  assert(mutationRateLimitExceeded(slots, 2, 0x22222222, 1003, 1, 1000));
}

int main() {
  test_allows_until_limit_then_blocks();
  test_resets_after_window();
  test_different_keys_use_different_slots();
  return 0;
}