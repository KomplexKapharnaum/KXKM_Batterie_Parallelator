#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct BmuCore;

/**
 * Demarre la tache BLE : init NimBLE puis notify a 2 Hz.
 * Pinned sur core 1, prio 3, stack 4096.
 */
void task_ble_start(struct BmuCore *core);

#ifdef __cplusplus
}
#endif
