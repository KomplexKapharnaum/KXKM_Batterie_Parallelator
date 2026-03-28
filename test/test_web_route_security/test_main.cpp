#include <cassert>

#include "../../src/WebRouteSecurity.h"

static void test_disabled_when_no_configured_token() {
  assert(!isMutationRouteEnabled(nullptr));
  assert(!isMutationRouteEnabled(""));
  assert(!isMutationTokenAuthorized("abc", ""));
}

static void test_rejects_missing_or_wrong_token() {
  assert(isMutationRouteEnabled("secret-token"));
  assert(!isMutationTokenAuthorized(nullptr, "secret-token"));
  assert(!isMutationTokenAuthorized("", "secret-token"));
  assert(!isMutationTokenAuthorized("wrong-token", "secret-token"));
  assert(!isMutationTokenAuthorized("secret-token ", "secret-token"));
  assert(!isMutationTokenAuthorized("secret-toke", "secret-token"));
  assert(!isMutationTokenAuthorized("secret-token-extra", "secret-token"));
}

static void test_accepts_matching_token() {
  assert(isMutationTokenAuthorized("secret-token", "secret-token"));
}

static void test_rejects_tokens_with_leading_or_trailing_spaces() {
  assert(isMutationRouteEnabled("secret-token"));
  assert(!isMutationTokenAuthorized(" secret-token", "secret-token"));
  assert(!isMutationTokenAuthorized("secret-token ", "secret-token"));
}

static void test_constant_time_behavior_empty_token_rejection() {
  assert(!isMutationRouteEnabled(""));
  assert(!isMutationTokenAuthorized("", ""));
  assert(!isMutationTokenAuthorized("some-token", ""));
}

static void test_constant_time_behavior_token_length_mismatch() {
  assert(isMutationRouteEnabled("secret-token"));
  assert(!isMutationTokenAuthorized("a", "secret-token"));
  assert(!isMutationTokenAuthorized("secret-token-extended", "secret-token"));
}

int main() {
  test_disabled_when_no_configured_token();
  test_rejects_missing_or_wrong_token();
  test_accepts_matching_token();
  test_rejects_tokens_with_leading_or_trailing_spaces();
  test_constant_time_behavior_empty_token_rejection();
  test_constant_time_behavior_token_length_mismatch();
  return 0;
}
