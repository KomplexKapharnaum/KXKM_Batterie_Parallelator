// firmware-idf-v2/main/task_ui.h
//
// Phase 17 -- tache FreeRTOS de refresh UI (LVGL 5 onglets, 5 Hz).

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct BmuCore;

// Demarre la tache UI sur APP_CPU (core 1), prio 2, stack 8192.
void task_ui_start(struct BmuCore *core);

#ifdef __cplusplus
}
#endif
