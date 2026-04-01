# Project Guidelines

## Scope
- This AGENTS.md is the workspace-wide instruction source; do not add a second global .github/copilot-instructions.md unless intentionally replacing it.
- This repository contains a BMU firmware stack for parallelized battery packs (up to 16 channels): PlatformIO/Arduino in firmware/ and an ESP-IDF track in firmware-idf/ for ESP32-S3-BOX-3.
- Keep behavior safe by default: battery protection logic must not be weakened.
- Prefer focused changes in firmware/src/ and firmware-idf/ and avoid unrelated edits in hardware design folders.

## Copilot Workflow
- Start by checking the scoped instruction file that matches the area you are editing.
- For firmware safety changes, inspect the relevant code path and the closest sim-host tests before proposing edits.
- Prefer minimal diffs, reuse existing validation/security helpers, and link to existing docs instead of rewriting them.
- Before substantial firmware changes, run pio test -e sim-host; before large changes, also run scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85.
- For risk-focused review work, prefer .github/agents/bmu-safety-review.agent.md or other narrow agents over generic broad reviews.

## Build, Upload, Test
- For a default firmware build, use pio run -e kxkm-s3-16MB.
- For upload to hardware, use pio run -e kxkm-s3-16MB --target upload only when the user explicitly asks to flash a connected board.
- For host-side validation, prefer pio test -e sim-host before hardware-oriented actions.
- For test diagnostics, use pio test -e sim-host -vv.
- For hardware serial logs after upload, use pio device monitor -e kxkm-s3-16MB --baud 115200.
- For ESP-IDF BOX-3 firmware builds, use: cd firmware-idf && idf.py build.
- For ESP-IDF flashing on BOX-3, use: cd firmware-idf && idf.py -p /dev/cu.usbmodem* flash.
- If ESP-IDF flashing fails with No serial data received, require manual BOOT+EN sequence before retrying.
- For ESP-IDF serial logs, use: cd firmware-idf && idf.py -p /dev/cu.usbmodem* monitor.
- If a change is broad enough to affect memory footprint, run scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85 after a successful build.

## Build and Test
- Main build (default): pio run -e kxkm-s3-16MB
- Upload firmware: pio run -e kxkm-s3-16MB --target upload
- ESP-IDF build (BOX-3): cd firmware-idf && idf.py build
- ESP-IDF UI config (BOX-3 display/touch): cd firmware-idf && idf.py menuconfig
- ESP-IDF flash (BOX-3): cd firmware-idf && idf.py -p /dev/cu.usbmodem* flash
- ESP-IDF monitor (BOX-3): cd firmware-idf && idf.py -p /dev/cu.usbmodem* monitor
- Legacy v3 build: pio run -e kxkm-v3-16MB
- Debug build: pio run -e esp-wrover-kit
- Serial monitor: pio device monitor -e kxkm-s3-16MB --baud 115200
- Host safety/security tests: pio test -e sim-host
- Verbose host test diagnostics: pio test -e sim-host -vv
- Memory guardrail (required before large changes): scripts/check_memory_budget.sh --env kxkm-s3-16MB --ram-max 75 --flash-max 85
- Hardware test target (when relevant): pio test -e kxkm-s3-16MB
- PlatformIO gotcha: keep framework=arduino only in [arduino_base] inside platformio.ini; setting it globally breaks the native/sim-host environments.

## Architecture
- Main entrypoint and orchestration (PlatformIO): firmware/src/main.cpp
- Core battery logic: firmware/src/BatteryParallelator.cpp, firmware/src/BatteryManager.cpp
- Sensor/actuator drivers: firmware/src/INAHandler.cpp, firmware/src/TCAHandler.cpp
- Shared bus lock: firmware/src/I2CMutex.h
- Logging and integrations: firmware/src/SD_Logger.cpp, firmware/src/InfluxDBHandler.cpp, firmware/src/WebServerHandler.cpp, firmware/src/TimeAndInfluxTask.cpp
- Main entrypoint and orchestration (ESP-IDF): firmware-idf/main/main.cpp
- ESP-IDF display path (BOX-3 BSP): firmware-idf/components/bmu_display/
- ESP-IDF BMU I2C bus path: firmware-idf/components/bmu_i2c/
- Topology is safety-critical: Nb_TCA * 4 must match Nb_INA or the firmware must stay fail-safe with batteries forced OFF.
- firmware/src/config.h is the source of truth for protection thresholds and timing values.

## Conventions
- The codebase is primarily French in comments and many identifiers; keep naming and phrasing consistent with surrounding code.
- Keep the existing style (Arduino/ESP32 C++, Doxygen-friendly headers, fixed-size arrays where already used).
- Any I2C access (Wire, INA/TCA operations) must use I2CLockGuard from firmware/src/I2CMutex.h.
- Validate sensor indexes before access and preserve existing fault behavior (NAN/early return patterns).
- Use existing DebugLogger categories for observability instead of ad-hoc Serial.print additions.
- Keep CSV conventions unchanged (; separator and existing timestamp flow in logger/influx code).

## ESP-IDF UI/UX (BOX-3 Display + Touch)
- UI stack is BSP + LVGL in firmware-idf/components/bmu_display/; keep initialization through bsp_display_start() and avoid custom parallel display init paths.
- Keep touch active through BSP input device wiring (bsp_display_get_input_dev + LV_EVENT_PRESSING) so wake/dim behavior remains deterministic.
- Treat the panel as ILI934x-class on BOX-3 (current code path uses ILI9342C-compatible BSP flow); do not hardcode alternate panel drivers in BMU component code.
- Keep UI refresh non-blocking: periodic update logic must stay short and must not introduce long waits inside display callbacks/timers.
- UI must remain advisory only: battery protection and switching decisions stay in bmu_protection / safety state machine, never in display handlers.
- When adding UI tabs/widgets, preserve lock discipline around LVGL calls (bsp_display_lock/unlock) and avoid direct cross-task mutation of shared battery state.

## Safety-Critical Rules
- Do not bypass topology validation between INA and TCA counts in firmware/src/main.cpp.
- Do not silently relax battery thresholds, reconnect delays, or current/voltage protections.
- Keep task behavior non-blocking for FreeRTOS loops and avoid long blocking calls in periodic tasks.
- On ESP-IDF BOX-3, do not create I2C bus 1 twice: BSP and BMU share DOCK pins (GPIO40/41). Initialize BSP I2C first, then reuse the existing DOCK bus handle in BMU I2C code.
- Keep BOX-3 internal display/touch bus ownership in BSP; BMU custom I2C flows must continue to use the dedicated BMU bus path.

## Scoped Instructions
- When editing firmware/src/**, follow .github/instructions/firmware-safety.instructions.md.
- When editing firmware-idf/**, still apply the same BMU safety invariants (topology fail-safe, no threshold relaxation, non-blocking task behavior).
- When editing firmware/src/Web*.{h,cpp}, WebRouteSecurity, WebMutationRateLimit, or BatteryRouteValidation, follow .github/instructions/webserver-safety.instructions.md.
- When editing scripts/ml/**, models/**, or docs/ml-battery-health-spec.md, follow .github/instructions/ml-pipeline.instructions.md.
- When editing plan/** or docs/superpowers/plans/**, follow .github/instructions/plan-todo-implementation.instructions.md.
- When editing Python tests under firmware/test/**, follow .github/instructions/tests-python.instructions.md.
- Prefer linking to the existing docs below instead of duplicating their content here.

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
- Architecture diagrams and governance context: docs/governance/architecture-diagrams.md
- ESP-IDF migration design: docs/superpowers/specs/2026-03-30-esp-idf-migration-design.md
- ESP-IDF display dashboard spec: docs/superpowers/specs/2026-03-30-phase6-display-dashboard.md
- ESP-IDF display enhancement spec: docs/superpowers/specs/2026-03-31-phase8-display-enhanced.md
- Current repository context snapshot: docs/context/project-context-snapshot.md
- ML battery health roadmap/spec: docs/ml-battery-health-spec.md
- Credentials template: firmware/src/credentials.h.example
- Platform/build configuration: platformio.ini
- Firmware safety instruction (applyTo src): .github/instructions/firmware-safety.instructions.md
- Web safety instruction (applyTo web routes): .github/instructions/webserver-safety.instructions.md
- ML pipeline instruction (applyTo scripts/ml, docs, models): .github/instructions/ml-pipeline.instructions.md
- BMU safety review agent: .github/agents/bmu-safety-review.agent.md
- Battery protection review agent: .github/agents/battery-protection-review.agent.md
- BMU audit prompt: .github/prompts/audit-bmu.prompt.md
- BMU preflight prompt: .github/prompts/preflight-bmu-change.prompt.md

## Areas to Avoid Unless Requested
- Legacy hardware folder marked as untouchable: hardware/battery-management-unit/PCB V1 (don't touch)/
- Large hardware/PCB/CAD assets under hardware/PCB/ and hardware/pcb-bmu-v2/ unless the task is explicitly hardware-design related.