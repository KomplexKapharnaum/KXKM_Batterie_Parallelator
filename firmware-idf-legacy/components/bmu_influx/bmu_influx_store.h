#pragma once
/**
 * bmu_influx_store — Buffer persistant pour données InfluxDB offline.
 *
 * Stockage primaire : partition FAT interne (/fatfs/influx/)
 * Fallback : carte SD si présente (/sdcard/influx/)
 * Rotation : 2 fichiers tournants, taille max configurable.
 * Replay : rejoue les fichiers vers InfluxDB quand la connexion revient.
 */

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Taille max d'un fichier de log avant rotation (octets) */
#define BMU_INFLUX_STORE_FILE_MAX   (512 * 1024)  /* 512 KB par fichier */

/** Nombre max de fichiers tournants (current + rotated) */
#define BMU_INFLUX_STORE_FILE_COUNT 2

/** Initialise le store. Appeler après bmu_fat_init() et optionnellement bmu_sd_init(). */
esp_err_t bmu_influx_store_init(void);

/** Persiste une ligne line-protocol sur le stockage le plus adapté.
 *  Appelé quand le flush HTTP échoue. */
esp_err_t bmu_influx_store_append(const char *line, size_t len);

/** Rejoue les fichiers persistés vers InfluxDB.
 *  Retourne le nombre de lignes rejouées, ou <0 en cas d'erreur. */
int bmu_influx_store_replay(void);

/** Retourne true si des données sont en attente de replay. */
bool bmu_influx_store_has_pending(void);

/** Taille totale des fichiers en attente (octets). */
size_t bmu_influx_store_pending_bytes(void);

#ifdef __cplusplus
}
#endif
