---
description: "Use when modifying BMU firmware safety logic, battery switching, FreeRTOS tasks, INA/TCA I2C access, or WebServer battery control endpoints. Enforces fail-safe rules and safety checks."
name: "Firmware Safety Rules"
applyTo: ["src/**/*.cpp", "src/**/*.h"]
---

# Firmware Safety Rules

- Keep protection behavior fail-safe by default: never weaken voltage/current thresholds, reconnect delays, or topology guards.
- Do not bypass INA/TCA topology validation in src/main.cpp.
- Any I2C access (Wire, INA/TCA operations) must use I2CLockGuard from src/I2CMutex.h.
- Validate battery/sensor indexes before array or driver access (isValidIndex/bounds checks).
- Preserve existing NAN/early-return fault semantics for sensor and bus failures.
- Keep periodic FreeRTOS tasks non-blocking; avoid long blocking calls inside task loops.
- For WebServer mutation routes, require explicit safety review and avoid unauthenticated ON/OFF control paths.
- For Influx/cloud code, do not use insecure TLS mode in production.
- Prefer DebugLogger categories over ad-hoc Serial.print for diagnostics.
- If safety behavior changes, include test updates for bounds, fault injection, and regression of protection transitions.

See: AGENTS.md, CLAUDE.md, README.md.