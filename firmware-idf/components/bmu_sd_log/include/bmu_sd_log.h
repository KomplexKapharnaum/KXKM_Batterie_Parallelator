// firmware-idf-v2/components/bmu_sd_log/include/bmu_sd_log.h
//
// Phase 15 -- writer line-protocol vers la partition FAT interne 11 MB.
//
// Le compose ecrit `bmu_telemetry,device=<MAC>,bat=<i> v=...,c=...,...
// <ts_ns>` (spec §8.3) dans `/fatfs/bmulog/bmu-YYYYMMDD-HHMMSS-NOSYNC.lp`.
//
// Files are 10 MB max each. Total directory cap (Phase 15) = 8 MB (la
// partition FAT v2 fait 11 MB, on garde 3 MB de marge). FIFO rotation :
// fichiers les plus vieux supprimes en premier.
//
// Suffix `-NOSYNC` reste systematique en Phase 15 -- MQTT n'est pas encore
// connecte (Phase 16). Le rename `-NOSYNC.lp` -> `.lp` interviendra dans
// task_mqtt apres ACK broker.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

struct BmuCore;
struct BmuSnapshotC;

#define BMU_SD_LOG_MOUNT       "/fatfs"
#define BMU_SD_LOG_DIR         "/fatfs/bmulog"
#define BMU_SD_LOG_MAX_BYTES   (8 * 1024 * 1024)   // 8 MB cap (partition 11 MB)
#define BMU_SD_LOG_FILE_BYTES  (1 * 1024 * 1024)   // rotate every 1 MB
                                                   // (10 MB en spec, mais 11 MB
                                                   // partition -> 1 MB pratique)

// Mount FAT partition + creation du dossier bmulog. Idempotent.
esp_err_t bmu_sd_log_init(void);

// True si init OK et le dossier est utilisable.
bool bmu_sd_log_is_ready(void);

// Append batch lignes line-protocol pour ce snapshot. Une ligne par battery
// 0..n_bat-1 + une ligne fleet/system.
esp_err_t bmu_sd_log_append(const struct BmuSnapshotC *snap);

// Spawne la tache periodique 10 s qui appelle append() avec le cached
// snapshot du core.
void task_sd_log_start(struct BmuCore *core);

// --- Replay cursor API (Phase 16) ---

typedef struct {
    char path[128];
    size_t file_size;
} bmu_sd_log_nosync_entry_t;

// List NOSYNC files in log dir. Returns count (max `max_entries`).
int bmu_sd_log_list_nosync(bmu_sd_log_nosync_entry_t *out, int max_entries);

// Rename file: remove -NOSYNC suffix (marks as synced).
esp_err_t bmu_sd_log_mark_synced(const char *nosync_path);

// Append a raw string line (used by MQTT to also log to SD).
esp_err_t bmu_sd_log_append_raw(const char *data, size_t len);

#ifdef __cplusplus
}
#endif
