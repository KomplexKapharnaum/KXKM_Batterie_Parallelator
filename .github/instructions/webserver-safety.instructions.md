---
description: "Use when modifying BMU web routes, WebSocket handlers, battery switch HTTP endpoints, route validation, auth token checks, or mutation rate limiting. Covers WebServerHandler, WebRouteSecurity, WebMutationRateLimit, and BatteryRouteValidation."
name: "WebServer Safety Rules"
applyTo: ["firmware/src/WebServerHandler.*", "firmware/src/WebRouteSecurity.*", "firmware/src/WebMutationRateLimit.*", "firmware/src/BatteryRouteValidation.*"]
---

# WebServer Safety Rules

- Treat every battery switch route and WebSocket mutation path as safety-sensitive.
- Do not add unauthenticated battery ON/OFF paths; keep token checks centralized through WebRouteSecurity.
- Do not bypass or weaken mutation throttling; reuse WebMutationRateLimit instead of rolling a second rate limiter.
- Keep request parsing and battery index validation centralized in BatteryRouteValidation.
- Reject malformed, out-of-range, or missing inputs explicitly before touching battery state.
- If a WebSocket path can mutate state, enforce the same auth, validation, and rate-limit guarantees as HTTP routes.
- Avoid duplicating security checks across handlers; refactor toward shared helpers when behavior changes.
- Preserve fail-safe behavior on validation/auth failure: deny the request, log through existing logging paths, and do not partially apply mutations.
- Add or update sim-host tests for auth, rate limiting, malformed indexes, and unauthorized mutation attempts when changing these flows.

## References
- [AGENTS](../../AGENTS.md)
- [Firmware Safety Instructions](./firmware-safety.instructions.md)
- [WebServerHandler](../../firmware/src/WebServerHandler.h)
- [WebRouteSecurity](../../firmware/src/WebRouteSecurity.h)
- [WebMutationRateLimit](../../firmware/src/WebMutationRateLimit.h)
- [BatteryRouteValidation](../../firmware/src/BatteryRouteValidation.h)