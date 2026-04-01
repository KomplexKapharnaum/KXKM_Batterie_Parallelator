---
description: "Use when editing ESP-IDF display/touch UI code (ILI9341/ILI934x + GT911) in firmware-idf/components/bmu_display. Enforces LVGL lock discipline, non-blocking UI, and strict separation from battery protection logic."
name: "ESP-IDF Display Safety Rules"
applyTo: "firmware-idf/components/bmu_display/**"
---

# ESP-IDF Display Safety Rules

- Keep UI initialization on BSP path (`bsp_display_start()` and related BSP helpers). Do not add alternate direct panel init in BMU display code.
- Keep touch active via BSP input-device wiring (`bsp_display_get_input_dev`) and preserve `LV_EVENT_PRESSING` wake/dim behavior.
- Keep LVGL calls protected with BSP lock discipline (`bsp_display_lock()` / `bsp_display_unlock()`).
- Keep refresh callbacks and periodic UI updates non-blocking. Do not introduce long waits, busy loops, or blocking network/I2C calls in display callbacks/timers.
- Treat UI as advisory only: never move battery protection or switching decisions into display handlers.
- Preserve bus ownership boundaries on BOX-3: internal display/touch bus remains BSP-owned; BMU custom I2C flows stay on BMU bus path.
- Keep panel compatibility in the existing ILI934x-class path; do not hardcode alternate display drivers in BMU component code.
- For UI state reads, avoid unsafe cross-task mutation of shared battery state.

## Validation Checklist

- Build passes: `cd firmware-idf && idf.py build`
- No direct battery switch mutation from UI callbacks
- No lockless LVGL write paths
- No new long blocking operations in display update path

## References
- [AGENTS](../../AGENTS.md)
- [CLAUDE](../../CLAUDE.md)
- [ESP-IDF migration design](../../docs/superpowers/specs/2026-03-30-esp-idf-migration-design.md)
- [Display dashboard spec](../../docs/superpowers/specs/2026-03-30-phase6-display-dashboard.md)
- [Display enhancement spec](../../docs/superpowers/specs/2026-03-31-phase8-display-enhanced.md)