// firmware-idf-v2/components/bmu_sd_log/src/bmu_sd_log.c
//
// Phase 15 -- voir bmu_sd_log.h pour le contrat.
//
// Mount : esp_vfs_fat_spiflash_mount_rw_wl sur la partition `fatfs` (cf
// firmware-idf-v2/partitions.csv : 0x520000 -> 0xAE0000 = 11 MB).
// Format : line protocol InfluxDB-style. NOSYNC suffix systematique.

#include "bmu_sd_log.h"
#include "bmu_core.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sdlog";

// --- Task params ---
#define TASK_PERIOD_MS_VAL  (10 * 1000)
#define TASK_PRIO_VAL       3
#define TASK_STACK_VAL      (4 * 1024)
#define PIN_APP_CPU_VAL     1

static bool s_ready = false;
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static char s_device_id[16] = {0};
static char s_log_dir[64] = BMU_SD_LOG_DIR;  // fallback a la racine du mount si mkdir KO
static char s_current_path[96] = {0};
static FILE *s_current_fp = NULL;
static size_t s_current_bytes = 0;

// --- Helpers ---

static void format_device_id(void)
{
    uint8_t mac[6] = {0};
    if (esp_efuse_mac_get_default(mac) != ESP_OK) {
        memset(mac, 0, 6);
    }
    snprintf(s_device_id, sizeof(s_device_id), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void make_filename(char *out, size_t out_sz)
{
    time_t now = time(NULL);
    struct tm tm_now;
    if (now < 1700000000) {
        // Pas de NTP -- utilise uptime ms comme tag d'unicite
        int64_t up = esp_timer_get_time() / 1000;
        snprintf(out, out_sz, "%s/bmu-up%011lld-NOSYNC.lp",
                 s_log_dir, (long long)up);
        return;
    }
    gmtime_r(&now, &tm_now);
    snprintf(out, out_sz, "%s/bmu-%04d%02d%02d-%02d%02d%02d-NOSYNC.lp",
             s_log_dir,
             tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
             tm_now.tm_hour, tm_now.tm_min, tm_now.tm_sec);
}

// Calcule la taille totale des .lp dans le dossier
static size_t dir_total_bytes(void)
{
    DIR *d = opendir(s_log_dir);
    if (d == NULL) return 0;
    size_t total = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char path[320];
        snprintf(path, sizeof(path), "%s/%s", s_log_dir, e->d_name);
        struct stat st;
        if (stat(path, &st) == 0) {
            total += st.st_size;
        }
    }
    closedir(d);
    return total;
}

// FIFO eviction : supprime les fichiers les plus vieux (par mtime).
static void enforce_size_cap(void)
{
    while (dir_total_bytes() > BMU_SD_LOG_MAX_BYTES) {
        DIR *d = opendir(s_log_dir);
        if (d == NULL) return;
        char oldest_path[320] = {0};
        time_t oldest_mtime = 0;
        bool first = true;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') continue;
            char path[320];
            snprintf(path, sizeof(path), "%s/%s", s_log_dir, e->d_name);
            // Skip current open file
            if (s_current_path[0] && strcmp(path, s_current_path) == 0) continue;
            struct stat st;
            if (stat(path, &st) != 0) continue;
            if (first || st.st_mtime < oldest_mtime) {
                strncpy(oldest_path, path, sizeof(oldest_path) - 1);
                oldest_mtime = st.st_mtime;
                first = false;
            }
        }
        closedir(d);
        if (oldest_path[0] == 0) break;
        ESP_LOGW(TAG, "size cap reached, deleting oldest: %s", oldest_path);
        if (unlink(oldest_path) != 0) {
            ESP_LOGE(TAG, "unlink failed");
            break;
        }
    }
}

static esp_err_t open_new_file(void)
{
    if (s_current_fp) {
        fclose(s_current_fp);
        s_current_fp = NULL;
    }
    make_filename(s_current_path, sizeof(s_current_path));
    s_current_fp = fopen(s_current_path, "a");
    if (s_current_fp == NULL) {
        ESP_LOGE(TAG, "fopen(%s) failed", s_current_path);
        s_current_path[0] = 0;
        return ESP_FAIL;
    }
    s_current_bytes = 0;
    ESP_LOGI(TAG, "opened %s", s_current_path);
    return ESP_OK;
}

// --- Public API ---

esp_err_t bmu_sd_log_init(void)
{
    if (s_ready) return ESP_OK;

    format_device_id();

    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = true,
        .max_files = 4,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(
        BMU_SD_LOG_MOUNT, "fatfs", &cfg, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_spiflash_mount_rw_wl failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    // Cree le dossier (EEXIST OK). Sur FAT certaines builds n'aiment pas
    // les sous-dossiers ; on tolere l'echec et ecrira a la racine du mount.
    int mr = mkdir(s_log_dir, 0775);
    if (mr != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir(%s) failed errno=%d -- falling back to mount root",
                 s_log_dir, errno);
        // Reduit le prefix au mount point lui-meme
        snprintf(s_log_dir, sizeof(s_log_dir), "%s", BMU_SD_LOG_MOUNT);
    } else {
        ESP_LOGI(TAG, "log dir %s ready", s_log_dir);
    }

    // Stats free
    uint64_t total_b = 0, free_b = 0;
    if (esp_vfs_fat_info(BMU_SD_LOG_MOUNT, &total_b, &free_b) == ESP_OK) {
        ESP_LOGI(TAG, "SD log mounted %s free=%llu kB total=%llu kB device=%s",
                 BMU_SD_LOG_MOUNT,
                 (unsigned long long)(free_b / 1024),
                 (unsigned long long)(total_b / 1024),
                 s_device_id);
    } else {
        ESP_LOGI(TAG, "SD log mounted %s device=%s", BMU_SD_LOG_MOUNT, s_device_id);
    }

    enforce_size_cap();
    if (open_new_file() != ESP_OK) {
        return ESP_FAIL;
    }

    s_ready = true;
    return ESP_OK;
}

bool bmu_sd_log_is_ready(void) { return s_ready; }

esp_err_t bmu_sd_log_append(const struct BmuSnapshotC *snap)
{
    if (!s_ready || snap == NULL) return ESP_ERR_INVALID_STATE;
    if (s_current_fp == NULL) {
        if (open_new_file() != ESP_OK) return ESP_FAIL;
    }

    // Timestamp ns wallclock si NTP, sinon uptime ns
    time_t now = time(NULL);
    int64_t ts_ns;
    if (now > 1700000000) {
        ts_ns = (int64_t)now * 1000000000LL;
    } else {
        ts_ns = esp_timer_get_time() * 1000LL;
    }

    char line[256];
    int written = 0;

    if (snap->n_bat == 0) {
        // Bench Phase 15 : ecris une ligne system pour valider le NOSYNC file.
        int n = snprintf(line, sizeof(line),
                         "bmu_telemetry,device=%s,bat=sys topo=%di,n_bat=%ui %lld\n",
                         s_device_id,
                         (int)snap->system.topology_ok,
                         (unsigned)snap->n_bat,
                         (long long)ts_ns);
        if (n > 0 && fwrite(line, 1, n, s_current_fp) == (size_t)n) {
            s_current_bytes += n;
            written += n;
        }
    } else {
        for (uint8_t i = 0; i < snap->n_bat && i < MAX_BATTERIES; i++) {
            const struct BmuBatteryC *b = &snap->batteries[i];
            int n = snprintf(line, sizeof(line),
                             "bmu_telemetry,device=%s,bat=%u "
                             "v=%ldi,c=%ldi,state=%ui,sc=%ui,ah=%ldi,soh=%ui %lld\n",
                             s_device_id,
                             (unsigned)i,
                             (long)b->voltage_mv,
                             (long)b->current_ma,
                             (unsigned)b->state,
                             (unsigned)b->switch_count,
                             (long)b->ah_remaining_ma_h,
                             (unsigned)b->soh_pct,
                             (long long)ts_ns);
            if (n > 0 && fwrite(line, 1, n, s_current_fp) == (size_t)n) {
                s_current_bytes += n;
                written += n;
            }
        }
    }

    fflush(s_current_fp);

    // Rotation si necessaire
    if (s_current_bytes >= BMU_SD_LOG_FILE_BYTES) {
        ESP_LOGI(TAG, "rotating: %s reached %u bytes",
                 s_current_path, (unsigned)s_current_bytes);
        fclose(s_current_fp);
        s_current_fp = NULL;
        enforce_size_cap();
        open_new_file();
    }

    return (written > 0) ? ESP_OK : ESP_FAIL;
}

// --- Task ---

static void task_sd_log_body(void *arg)
{
    struct BmuCore *core = (struct BmuCore *)arg;
    ESP_LOGI(TAG, "task_sd_log started: core=APP_CPU prio=%d period=%d ms ready=%d",
             TASK_PRIO_VAL, TASK_PERIOD_MS_VAL, (int)s_ready);

    struct BmuSnapshotC snap;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        if (s_ready && core != NULL) {
            memset(&snap, 0, sizeof(snap));
            int rc = bmu_core_get_cached_snapshot(core, &snap);
            if (rc == 0) {
                esp_err_t aerr = bmu_sd_log_append(&snap);
                if (aerr != ESP_OK) {
                    ESP_LOGW(TAG, "append failed: %s", esp_err_to_name(aerr));
                }
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TASK_PERIOD_MS_VAL));
    }
}

void task_sd_log_start(struct BmuCore *core)
{
    TaskHandle_t h = NULL;
    BaseType_t ok = xTaskCreatePinnedToCore(
        task_sd_log_body,
        "sd_log",
        TASK_STACK_VAL,
        (void *)core,
        TASK_PRIO_VAL,
        &h,
        PIN_APP_CPU_VAL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore(task_sd_log) failed");
        return;
    }
    ESP_LOGI(TAG, "task_sd_log created handle=%p pinned APP_CPU prio=%d",
             (void *)h, TASK_PRIO_VAL);
}
