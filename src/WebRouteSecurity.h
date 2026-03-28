#ifndef WEB_ROUTE_SECURITY_H
#define WEB_ROUTE_SECURITY_H

bool isMutationRouteEnabled(const char *configuredToken);
bool isMutationTokenAuthorized(const char *providedToken,
                               const char *configuredToken);

#endif // WEB_ROUTE_SECURITY_H
