/**
 * bmu_influx_store — Buffer persistant InfluxDB avec rotation et fallback SD.
 *
 * Stratégie :
 *   1. Écrire dans /fatfs/influx/current.lp (line-protocol)
 *   2. Quand current.lp dépasse FILE_MAX → renommer en rotated.lp (écrase l'ancien)
 *   3. Si FAT plein ou non monté → fallback sur /sdcard/influx/
 *   4. Replay : lire rotated.lp puis current.lp, envoyer ligne par ligne via bmu_influx_flush
 */

#include "bmu_influx_store.h"
#include "bmu_influx.h"
#include "bmu_storage.h"

#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>

static const char *TAG = "INFLUX_STORE";

/* ── Chemins ─────────────────────────────────────────────────────────── */

static const char *FAT_DIR      = BMU_FAT_MOUNT "/influx";
static const char *FAT_CURRENT  = BMU_FAT_MOUNT "/influx/current.lp";
static const char *FAT_ROTATED  = BMU_FAT_MOUNT "/influx/rotated.lp";

static const char *SD_DIR       = BMU_SD_MOUNT "/influx";
static const char *SD_CURRENT   = BMU_SD_MOUNT "/influx/current.lp";
static const char *SD_ROTATED   = BMU_SD_MOUNT "/influx/rotated.lp";

static bool s_initialized = false;
static bool s_use_sd = false;  /* true si FAT indisponible, fallback SD */

/* ── Helpers ─────────────────────────────────────────────────────────── */

static bool dir_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static bool ensure_dir(const char *path)
{
    if (dir_exists(path)) return true;
    int ret = mkdir(path, 0755);
    if (ret == 0) {
        ESP_LOGI(TAG, "Répertoire créé: %s", path);
        return true;
    }
    ESP_LOGW(TAG, "mkdir(%s) échoué", path);
    return false;
}

static long file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_size;
}

static const char *current_path(void) { return s_use_sd ? SD_CURRENT : FAT_CURRENT; }
static const char *rotated_path(void) { return s_use_sd ? SD_ROTATED : FAT_ROTATED; }

/* ── Rotation ────────────────────────────────────────────────────────── */

static void rotate_if_needed(void)
{
    long sz = file_size(current_path());
    if (sz < BMU_INFLUX_STORE_FILE_MAX) return;

    /* Supprimer l'ancien rotated, renommer current → rotated */
    remove(rotated_path());
    if (rename(current_path(), rotated_path()) == 0) {
        ESP_LOGI(TAG, "Rotation: %s → %s (%ld octets)", current_path(), rotated_path(), sz);
    } else {
        /* rename échoué (cross-device?) — tronquer */
        ESP_LOGW(TAG, "Rename échoué, truncation de %s", current_path());
        FILE *f = fopen(current_path(), "w");
        if (f) fclose(f);
    }
}

/* ── Init ────────────────────────────────────────────────────────────── */

esp_err_t bmu_influx_store_init(void)
{
    s_use_sd = false;

    /* Priorité FAT interne (wear-leveled, toujours disponible) */
    if (bmu_fat_is_mounted() && ensure_dir(FAT_DIR)) {
        s_use_sd = false;
        s_initialized = true;
        ESP_LOGI(TAG, "Store init sur FAT interne (%s)", FAT_DIR);
        return ESP_OK;
    }

    /* Fallback SD si présente */
    if (bmu_sd_is_mounted() && ensure_dir(SD_DIR)) {
        s_use_sd = true;
        s_initialized = true;
        ESP_LOGI(TAG, "Store init sur carte SD (fallback) (%s)", SD_DIR);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Aucun stockage disponible — données offline seront perdues");
    return ESP_ERR_NOT_FOUND;
}

/* ── Append ──────────────────────────────────────────────────────────── */

esp_err_t bmu_influx_store_append(const char *line, size_t len)
{
    if (!s_initialized || line == nullptr || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    rotate_if_needed();

    FILE *f = fopen(current_path(), "a");
    if (f == nullptr) {
        ESP_LOGW(TAG, "Impossible d'ouvrir %s", current_path());
        return ESP_FAIL;
    }

    size_t written = fwrite(line, 1, len, f);
    /* S'assurer que la ligne se termine par \n */
    if (len > 0 && line[len - 1] != '\n') {
        fputc('\n', f);
    }
    fclose(f);

    return (written == len) ? ESP_OK : ESP_FAIL;
}

/* ── Replay ──────────────────────────────────────────────────────────── */

static int replay_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == nullptr) return 0;

    char line[512];
    int count = 0;
    int errors = 0;

    while (fgets(line, sizeof(line), f) != nullptr) {
        size_t len = strlen(line);
        /* Retirer le \n final */
        if (len > 0 && line[len - 1] == '\n') {
            line[--len] = '\0';
        }
        if (len == 0) continue;

        /* Parser la ligne line-protocol et la re-soumettre au buffer mémoire.
         * On utilise bmu_influx_write brut — la ligne est déjà formatée.
         * Ajout direct dans le buffer statique de bmu_influx. */
        esp_err_t ret = bmu_influx_write_raw(line, len);
        if (ret == ESP_OK) {
            count++;
        } else {
            errors++;
            if (errors > 5) {
                ESP_LOGW(TAG, "Trop d'erreurs replay — arrêt (connexion perdue ?)");
                break;
            }
        }
    }

    fclose(f);

    if (errors == 0) {
        /* Fichier entièrement rejoué — supprimer */
        remove(path);
        ESP_LOGI(TAG, "Replay %s terminé: %d lignes", path, count);
    } else {
        ESP_LOGW(TAG, "Replay %s partiel: %d OK, %d erreurs", path, count, errors);
    }

    return count;
}

int bmu_influx_store_replay(void)
{
    if (!s_initialized) return -1;

    int total = 0;
    /* Rejouer l'ancien fichier en premier (données les plus anciennes) */
    total += replay_file(rotated_path());
    total += replay_file(current_path());

    /* Flush final pour envoyer ce qui reste dans le buffer mémoire */
    if (total > 0) {
        bmu_influx_flush();
    }

    return total;
}

/* ── Status ──────────────────────────────────────────────────────────── */

bool bmu_influx_store_has_pending(void)
{
    if (!s_initialized) return false;
    return (file_size(current_path()) > 0 || file_size(rotated_path()) > 0);
}

size_t bmu_influx_store_pending_bytes(void)
{
    if (!s_initialized) return 0;
    long sz = file_size(current_path()) + file_size(rotated_path());
    return (sz > 0) ? (size_t)sz : 0;
}
