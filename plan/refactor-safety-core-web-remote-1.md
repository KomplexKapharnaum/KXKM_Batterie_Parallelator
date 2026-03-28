---
goal: "Phase 1 and Phase 2 Implementation Plan: Safety Core Stabilization and Web/Remote Hardening"
version: "1.0"
date_created: "2026-03-28"
last_updated: "2026-03-28"
owner: "Firmware BMU Team"
status: "In progress"
tags: ["refactor", "safety", "security", "freertos", "web", "esp32-s3"]
---

# Introduction

![Status: In progress](https://img.shields.io/badge/status-In%20progress-yellow)

This plan implements only Phase 1 and Phase 2 of the BMU refactor. Phase 1 stabilizes local safety runtime (watchdog, I2C recovery, safe switching path integrity). Phase 2 hardens web/remote mutation paths (mandatory auth, strict validation, rate limiting, audit logging) without weakening protection logic.

## 1. Requirements & Constraints

- **REQ-001**: Battery protection state decisions must remain 100% local on MCU and must not depend on cloud connectivity.
- **REQ-002**: Switching operations must use a single validated control path and preserve existing threshold protections.
- **REQ-003**: All I2C interactions must remain serialized with `I2CLockGuard` from `src/I2CMutex.h`.
- **REQ-004**: Web mutation routes must require authentication before executing switch operations.
- **REQ-005**: Battery index parsing for mutation routes must be strict and bounds-checked.
- **SEC-001**: Do not introduce unauthenticated battery mutation paths.
- **SEC-002**: Do not relax voltage/current/reconnect/topology safety checks.
- **SEC-003**: Mutation endpoints must include abuse protection (rate limiting) and auditability.
- **CON-001**: Keep FreeRTOS task loops non-blocking; avoid long blocking calls in periodic loops.
- **CON-002**: Preserve compatibility with `pio run -e kxkm-s3-16MB` and existing `kxkm-v3-16MB` behavior.
- **GUD-001**: Use existing `DebugLogger` categories for observability instead of ad-hoc serial prints.
- **PAT-001**: Implement changes as small, verifiable increments with build validation after each increment.

## 2. Implementation Steps

### Implementation Phase 1

- GOAL-001: Stabilize local safety runtime by reducing failure propagation in task execution and I2C communication paths.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-001 | Add task watchdog integration in `src/main.cpp` using `esp_task_wdt_init`, task registration in `logDataTask`, `checkBatteryTask`, `sendStoredDataTask`, and `webServerTask`, plus periodic `esp_task_wdt_reset`. | ✅ | 2026-03-28 |
| TASK-002 | Add shared I2C failure counter and recovery threshold helpers in `src/I2CMutex.h` (`g_i2cConsecutiveFailures`, `i2cRecordFailure`, `i2cResetFailureCounter`, `i2cShouldRecover`). | ✅ | 2026-03-28 |
| TASK-003 | Integrate conditional I2C recovery in `src/INA_NRJ_lib.cpp` on lock failures and repeated sensor errors using `i2cBusRecovery`. | ✅ | 2026-03-28 |
| TASK-004 | Integrate conditional I2C recovery in `src/TCA_NRJ_lib.cpp` on lock failures for read/write paths using existing mutex discipline. | ✅ | 2026-03-28 |
| TASK-005 | Add explicit runtime validation task for brownout handling policy: define implementation in `src/main.cpp` for restart-loop detection and safe-mode trigger gate (without changing switch authority). |  |  |
| TASK-006 | Add shared-state confinement for battery runtime structures (`battery_voltages`, reconnect counters) with a dedicated synchronization strategy in `src/BatteryParallelator.cpp` and `src/main.cpp`. |  |  |
| TASK-007 | Validate Phase 1 by building `kxkm-s3-16MB` and capturing memory footprint guardrail output for regression tracking. | ✅ | 2026-03-28 |

### Implementation Phase 2

- GOAL-002: Harden web/remote mutation surface with strict auth, strict input validation, abuse resistance, and audit logging.

| Task | Description | Completed | Date |
|------|-------------|-----------|------|
| TASK-008 | Enforce mutation auth guard in `src/WebServerHandler.cpp` for `/switch_on` and `/switch_off` via `authorizeBatteryMutationRequest`. | ✅ | 2026-03-28 |
| TASK-009 | Enforce strict battery argument parsing and bounds validation in `src/WebServerHandler.cpp` via `parseBatteryIndex` (`src/BatteryRouteValidation.cpp`). | ✅ | 2026-03-28 |
| TASK-010 | Add request-level rate limiting for mutation routes in `src/WebServerHandler.cpp` using IP/time window counters and deterministic HTTP 429 responses. |  |  |
| TASK-011 | Add structured audit log entries for every mutation attempt (success/failure/auth error/rate limit) in `src/WebServerHandler.cpp` with fields: route, battery, source, outcome, timestamp. |  |  |
| TASK-012 | Add token handling hardening in `src/WebRouteSecurity.cpp`: constant-time compare helper and explicit empty-token rejection tests. |  |  |
| TASK-013 | Extend tests in `test/test_battery_route_validation/test_main.cpp` and `test/test_web_route_security/test_main.cpp` for malformed inputs, replay attempts, and rate-limit boundary behavior. |  |  |
| TASK-014 | Build validation after Phase 2 changes with `pio run -e kxkm-s3-16MB` and no new compile errors. |  |  |

## 3. Alternatives

- **ALT-001**: Implement Phase 2 before Phase 1. Not chosen because runtime instability (watchdog/I2C) increases risk during web-hardening rollout.
- **ALT-002**: Introduce cloud-authorized switching gate. Not chosen because requirement mandates local-only switching authority.
- **ALT-003**: Add broad refactor across all modules in one change set. Not chosen to avoid high regression risk in safety-critical firmware.

## 4. Dependencies

- **DEP-001**: ESP32 Arduino support for task watchdog (`esp_task_wdt.h`) available in current framework.
- **DEP-002**: Existing mutex infrastructure in `src/I2CMutex.h` must remain authoritative for Wire bus access.
- **DEP-003**: Existing web stack (`ESPAsyncWebServer`) and route wiring in `src/WebServerHandler.cpp`.
- **DEP-004**: Existing strict battery parser helper in `src/BatteryRouteValidation.cpp`.
- **DEP-005**: Existing route security helper in `src/WebRouteSecurity.cpp`.

## 5. Files

- **FILE-001**: `src/main.cpp` — task watchdog initialization, task registration, periodic reset points, future brownout/restart-loop gate.
- **FILE-002**: `src/I2CMutex.h` — I2C failure counters and recovery threshold helpers.
- **FILE-003**: `src/INA_NRJ_lib.cpp` — I2C recovery trigger in INA read paths.
- **FILE-004**: `src/TCA_NRJ_lib.cpp` — I2C recovery trigger in TCA read/write lock-failure paths.
- **FILE-005**: `src/WebServerHandler.cpp` — auth guard, strict validation, pending rate limit, pending audit logs.
- **FILE-006**: `src/WebRouteSecurity.cpp` — token authorization hardening.
- **FILE-007**: `src/BatteryRouteValidation.cpp` — strict input parser used by mutation routes.
- **FILE-008**: `test/test_battery_route_validation/test_main.cpp` — parser validation tests.
- **FILE-009**: `test/test_web_route_security/test_main.cpp` — route security tests.

## 6. Testing

- **TEST-001**: Compile test `pio run -e kxkm-s3-16MB` must succeed after each phase increment.
- **TEST-002**: Mutation route auth test: unauthorized calls must return HTTP 403.
- **TEST-003**: Battery parameter validation test: malformed/out-of-range arguments must return HTTP 400.
- **TEST-004**: I2C recovery threshold test: force repeated lock failures and verify recovery path triggers without deadlock.
- **TEST-005**: Watchdog liveness test: simulate stalled task and verify watchdog reset behavior.
- **TEST-006**: Rate limit test (Phase 2 pending): repeated mutation attempts beyond threshold must return HTTP 429.
- **TEST-007**: Audit log test (Phase 2 pending): every mutation attempt emits a structured log entry with deterministic fields.

## 7. Risks & Assumptions

- **RISK-001**: Watchdog timeout set too low can cause false resets under transient load spikes.
- **RISK-002**: Aggressive I2C recovery can mask intermittent hardware faults if not paired with fault counters/alerts.
- **RISK-003**: Rate limiting can block legitimate maintenance operations if thresholds are misconfigured.
- **ASSUMPTION-001**: Existing deployment keeps `BMU_WEB_ADMIN_TOKEN` configured in environments where remote mutation is required.
- **ASSUMPTION-002**: Current hardware wiring allows safe `i2cBusRecovery` sequence without side effects on all connected peripherals.
- **ASSUMPTION-003**: Brownout implementation details may require additional board-specific validation before enablement.

## 8. Related Specifications / Further Reading

- `AGENTS.md`
- `.github/instructions/firmware-safety.instructions.md`
- `CLAUDE.md`
- `platformio.ini`
- `src/main.cpp`
- `src/BatteryParallelator.cpp`
- `src/WebServerHandler.cpp`