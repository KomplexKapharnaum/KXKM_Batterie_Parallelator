#include "WebRouteSecurity.h"

#include <cstring>

static bool constantTimeEquals(const char *a, const char *b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }

  const size_t lenA = std::strlen(a);
  const size_t lenB = std::strlen(b);
  const size_t maxLen = (lenA > lenB) ? lenA : lenB;

  unsigned char diff = static_cast<unsigned char>(lenA ^ lenB);
  for (size_t i = 0; i < maxLen; ++i) {
    const unsigned char ca = (i < lenA) ? static_cast<unsigned char>(a[i]) : 0;
    const unsigned char cb = (i < lenB) ? static_cast<unsigned char>(b[i]) : 0;
    diff |= static_cast<unsigned char>(ca ^ cb);
  }

  return diff == 0;
}

bool isMutationRouteEnabled(const char *configuredToken) {
  return configuredToken != nullptr && configuredToken[0] != '\0';
}

bool isMutationTokenAuthorized(const char *providedToken,
                               const char *configuredToken) {
  if (!isMutationRouteEnabled(configuredToken)) {
    return false;
  }

  if (providedToken == nullptr || providedToken[0] == '\0') {
    return false;
  }

  return constantTimeEquals(providedToken, configuredToken);
}
