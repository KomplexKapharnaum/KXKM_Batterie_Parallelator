# Project Guidelines

## Scope
- This repository is an ESP32 PlatformIO battery management firmware (BMU) for parallelized battery packs (up to 16 channels).
- Keep behavior safe by default: battery protection logic must not be weakened.
- Prefer focused changes in firmware/src/ and avoid unrelated edits in hardware design folders.

## Build and Test
- Main build (default): pio run -e kxkm-s3-16MB
- Upload firmware: pio run -e kxkm-s3-16MB --target upload
- Legacy v3 build: pio run -e kxkm-v3-16MB
- Debug build: pio run -e esp-wrover-kit
- Serial monitor: pio device monitor -e kxkm-s3-16MB --baud 115200
- Host safety/security tests: pio test -e sim-host
- Memory guardrail (required before large changes): scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85
- Hardware test target (when relevant): pio test -e kxkm-s3-16MB

## Architecture
- Main entrypoint and orchestration: firmware/src/main.cpp
- Core battery logic: firmware/src/BatteryParallelator.cpp, firmware/src/BatteryManager.cpp
- Sensor/actuator drivers: firmware/src/INAHandler.cpp, firmware/src/TCAHandler.cpp
- Shared bus lock: firmware/src/I2CMutex.h
- Logging and integrations: firmware/src/SD_Logger.cpp, firmware/src/InfluxDBHandler.cpp, firmware/src/WebServerHandler.cpp, firmware/src/TimeAndInfluxTask.cpp

## Conventions
- The codebase is primarily French in comments and many identifiers; keep naming and phrasing consistent with surrounding code.
- Keep the existing style (Arduino/ESP32 C++, Doxygen-friendly headers, fixed-size arrays where already used).
- Any I2C access (Wire, INA/TCA operations) must use I2CLockGuard from firmware/src/I2CMutex.h.
- Validate sensor indexes before access and preserve existing fault behavior (NAN/early return patterns).
- Use existing DebugLogger categories for observability instead of ad-hoc Serial.print additions.
- Keep CSV conventions unchanged (; separator and existing timestamp flow in logger/influx code).

## Safety-Critical Rules
- Do not bypass topology validation between INA and TCA counts in firmware/src/main.cpp.
- Do not silently relax battery thresholds, reconnect delays, or current/voltage protections.
- Keep task behavior non-blocking for FreeRTOS loops and avoid long blocking calls in periodic tasks.

## Audit Focus
- Prioritize security and reliability risks before feature work.
- Web control paths are safety-sensitive: review battery switch endpoints and avoid unauthenticated mutation routes in WebServer code.
- For cloud upload, avoid insecure TLS modes in production.
- Audit concurrent access to shared battery state across FreeRTOS tasks.
- Keep protection thresholds and offset logic consistent between task code and battery control classes.
- Prevent permanent task stop on recoverable runtime errors (SD/WiFi/network).
- Expand tests beyond single hardware smoke test: bounds, concurrency, fault injection, and long-run stability.
- For route changes, keep request validation centralized (BatteryRouteValidation/WebRouteSecurity) rather than duplicating checks.

## SOTA 2026 Direction
- Prefer a hybrid strategy: deterministic local protection on MCU, predictive analytics and long-horizon RUL in cloud.
- For edge inference, prefer compact quantized INT8 models with bounded latency and predictable memory use.
- Keep ML advisory: ML outputs must assist, never replace, hard safety guards and protection state machines.
- Use reproducible ML governance: dataset and feature versioning, repeatable benchmarks, drift checks, and explicit confidence labels.
- Validate with both field logs and public PHM datasets; track out-of-domain performance separately from in-domain metrics.

## Key References (Link, do not duplicate)
- Project architecture and operating notes: CLAUDE.md
- Product and usage overview: README.md
- ML battery health roadmap/spec: docs/ml-battery-health-spec.md
- Credentials template: firmware/src/credentials.h.example
- Platform/build configuration: platformio.ini
- Firmware safety instruction (applyTo src): .github/instructions/firmware-safety.instructions.md
- ML pipeline instruction (applyTo scripts/ml, docs, models): .github/instructions/ml-pipeline.instructions.md
- BMU safety review agent: .github/agents/bmu-safety-review.agent.md
- BMU audit prompt: .github/prompts/audit-bmu.prompt.md

## Areas to Avoid Unless Requested
- Legacy hardware folder marked as untouchable: hardware/battery-management-unit/PCB V1 (don't touch)/
- Large hardware/PCB/CAD assets under hardware/PCB/ and hardware/pcb-bmu-v2/ unless the task is explicitly hardware-design related.