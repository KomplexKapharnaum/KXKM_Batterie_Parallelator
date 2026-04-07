#include <cassert>

#include "../../src/WebRouteSecurity.h"
#include "../../src/WebMutationRateLimit.h"

static constexpr uint8_t MAX_REQUESTS = 10;
static constexpr uint32_t WINDOW_MS = 10000;
static constexpr int SLOT_COUNT = 8;

static void test_full_auth_flow_accepted() {
  const char *configured = "my-secret";
  MutationRateLimitSlot slots[SLOT_COUNT] = {};
  const uint32_t ip = 0xC0A80001; // 192.168.0.1

  // Token auth passes
  assert(isMutationRouteEnabled(configured));
  assert(isMutationTokenAuthorized("my-secret", configured));

  // Rate limit allows
  assert(!mutationRateLimitExceeded(slots, SLOT_COUNT, ip, 1000,
                                    MAX_REQUESTS, WINDOW_MS));

  // Both conditions met = request allowed
}

static void test_full_auth_flow_rejected_bad_token() {
  const char *configured = "my-secret";
  MutationRateLimitSlot slots[SLOT_COUNT] = {};
  const uint32_t ip = 0xC0A80001;

  // Rate limit would allow
  assert(!mutationRateLimitExceeded(slots, SLOT_COUNT, ip, 1000,
                                    MAX_REQUESTS, WINDOW_MS));

  // But auth fails with wrong token = request rejected
  assert(isMutationRouteEnabled(configured));
  assert(!isMutationTokenAuthorized("wrong-token", configured));
}

static void test_full_auth_flow_rejected_rate_limited() {
  const char *configured = "my-secret";
  MutationRateLimitSlot slots[SLOT_COUNT] = {};
  const uint32_t ip = 0xC0A80001;

  // Token auth passes
  assert(isMutationRouteEnabled(configured));
  assert(isMutationTokenAuthorized("my-secret", configured));

  // Exhaust rate limit: 10 requests allowed
  for (uint8_t i = 0; i < MAX_REQUESTS; i++) {
    assert(!mutationRateLimitExceeded(slots, SLOT_COUNT, ip, 1000 + i,
                                      MAX_REQUESTS, WINDOW_MS));
  }

  // 11th request rejected despite valid token
  assert(mutationRateLimitExceeded(slots, SLOT_COUNT, ip, 1010,
                                   MAX_REQUESTS, WINDOW_MS));
}

static void test_bearer_then_rate_limit_flow() {
  const char *configured = "my-secret";
  MutationRateLimitSlot slots[SLOT_COUNT] = {};
  const uint32_t ip = 0xC0A80002; // 192.168.0.2

  // Bearer header auth passes
  assert(isMutationAuthorizationHeaderAuthorized("Bearer my-secret",
                                                 configured));

  // Exhaust rate limit on same IP key
  for (uint8_t i = 0; i < MAX_REQUESTS; i++) {
    assert(!mutationRateLimitExceeded(slots, SLOT_COUNT, ip, 2000 + i,
                                      MAX_REQUESTS, WINDOW_MS));
  }

  // Rate limited even though auth is valid
  assert(mutationRateLimitExceeded(slots, SLOT_COUNT, ip, 2010,
                                   MAX_REQUESTS, WINDOW_MS));
}

static void test_disabled_token_blocks_all_even_if_rate_ok() {
  MutationRateLimitSlot slots[SLOT_COUNT] = {};
  const uint32_t ip = 0xC0A80003;

  // Rate limit would allow
  assert(!mutationRateLimitExceeded(slots, SLOT_COUNT, ip, 3000,
                                    MAX_REQUESTS, WINDOW_MS));

  // But no configured token = mutations disabled
  assert(!isMutationRouteEnabled(""));
  assert(!isMutationRouteEnabled(nullptr));
}

static void test_rate_limit_resets_after_window() {
  const char *configured = "my-secret";
  MutationRateLimitSlot slots[SLOT_COUNT] = {};
  const uint32_t ip = 0xC0A80004;

  assert(isMutationRouteEnabled(configured));
  assert(isMutationTokenAuthorized("my-secret", configured));

  // Exhaust limit at t=1000
  for (uint8_t i = 0; i < MAX_REQUESTS; i++) {
    assert(!mutationRateLimitExceeded(slots, SLOT_COUNT, ip, 1000 + i,
                                      MAX_REQUESTS, WINDOW_MS));
  }
  // Blocked at t=1010
  assert(mutationRateLimitExceeded(slots, SLOT_COUNT, ip, 1010,
                                   MAX_REQUESTS, WINDOW_MS));

  // After 10s window (t=12000), requests allowed again
  assert(!mutationRateLimitExceeded(slots, SLOT_COUNT, ip, 12000,
                                    MAX_REQUESTS, WINDOW_MS));
}

static void test_multiple_ips_independent_rate_limits() {
  MutationRateLimitSlot slots[SLOT_COUNT] = {};
  const uint32_t ip_a = 0xC0A80010; // 192.168.0.16
  const uint32_t ip_b = 0xC0A80020; // 192.168.0.32

  // Exhaust limit for IP A
  for (uint8_t i = 0; i < MAX_REQUESTS; i++) {
    assert(!mutationRateLimitExceeded(slots, SLOT_COUNT, ip_a, 5000 + i,
                                      MAX_REQUESTS, WINDOW_MS));
  }
  // IP A is now blocked
  assert(mutationRateLimitExceeded(slots, SLOT_COUNT, ip_a, 5010,
                                   MAX_REQUESTS, WINDOW_MS));

  // IP B still has its own independent window — first request allowed
  assert(!mutationRateLimitExceeded(slots, SLOT_COUNT, ip_b, 5011,
                                    MAX_REQUESTS, WINDOW_MS));

  // Exhaust limit for IP B independently
  for (uint8_t i = 1; i < MAX_REQUESTS; i++) {
    assert(!mutationRateLimitExceeded(slots, SLOT_COUNT, ip_b, 5011 + i,
                                      MAX_REQUESTS, WINDOW_MS));
  }
  // IP B is now blocked too
  assert(mutationRateLimitExceeded(slots, SLOT_COUNT, ip_b, 5021,
                                   MAX_REQUESTS, WINDOW_MS));

  // IP A is still blocked (same window)
  assert(mutationRateLimitExceeded(slots, SLOT_COUNT, ip_a, 5022,
                                   MAX_REQUESTS, WINDOW_MS));
}

int main() {
  test_full_auth_flow_accepted();
  test_full_auth_flow_rejected_bad_token();
  test_full_auth_flow_rejected_rate_limited();
  test_bearer_then_rate_limit_flow();
  test_disabled_token_blocks_all_even_if_rate_ok();
  test_rate_limit_resets_after_window();
  test_multiple_ips_independent_rate_limits();
  return 0;
}
