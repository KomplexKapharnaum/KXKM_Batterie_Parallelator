/**
 * bmu_storage.cpp — SD card logger (SPI on PMOD2) + NVS credentials
 *
 * SD : FAT32 via SPI bus, fichier CSV append-only
 * NVS : namespace "bmu" pour WiFi/MQTT credentials et config persistante
 */

#include "bmu_storage.h"

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

#include <cstring>
#include <cstdio>

/* ── SD Card ─────────────────────────────────────────────────────────── */

static const char *TAG_SD  = "SD";
static const char *TAG_NVS = "NVS";

static bool s_sd_mounted = false;
static sdmmc_card_t *s_card = nullptr;

#define BMU_SD_LOG_PATH BMU_SD_MOUNT "/bmu_log.csv"
#define BMU_SD_SPI_HOST SPI2_HOST

esp_err_t bmu_sd_init(void)
{
    if (s_sd_mounted) {
        return ESP_OK;
    }

    /* ── SPI bus ── */
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num     = BMU_SD_MOSI;
    bus_cfg.miso_io_num     = BMU_SD_MISO;
    bus_cfg.sclk_io_num     = BMU_SD_CLK;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    bus_cfg.max_transfer_sz = 4096;

    esp_err_t ret = spi_bus_initialize(BMU_SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_SD, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* ── SDMMC host en mode SPI ── */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BMU_SD_SPI_HOST;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs   = BMU_SD_CS;
    slot_cfg.host_id   = BMU_SD_SPI_HOST;

    /* ── Mount FAT ── */
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
    mount_cfg.format_if_mount_failed = false;
    mount_cfg.max_files              = 4;
    mount_cfg.allocation_unit_size   = 16 * 1024;

    ret = esp_vfs_fat_sdspi_mount(BMU_SD_MOUNT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_SD, "Montage SD échoué: %s — BMU fonctionne sans carte SD",
                 esp_err_to_name(ret));
        spi_bus_free(BMU_SD_SPI_HOST);
        return ESP_ERR_NOT_FOUND;
    }

    sdmmc_card_print_info(stdout, s_card);
    s_sd_mounted = true;
    ESP_LOGI(TAG_SD, "Carte SD montée sur %s", BMU_SD_MOUNT);
    return ESP_OK;
}

bool bmu_sd_is_mounted(void)
{
    return s_sd_mounted;
}

esp_err_t bmu_sd_log_line(const char *line)
{
    if (!s_sd_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (line == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(BMU_SD_LOG_PATH, "a");
    if (f == nullptr) {
        ESP_LOGW(TAG_SD, "Impossible d'ouvrir %s en écriture", BMU_SD_LOG_PATH);
        return ESP_FAIL;
    }

    fprintf(f, "%s\n", line);
    fclose(f);
    return ESP_OK;
}

esp_err_t bmu_sd_read_last_lines(char *buf, size_t buf_size, int max_lines)
{
    if (!s_sd_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (buf == nullptr || buf_size == 0 || max_lines <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    FILE *f = fopen(BMU_SD_LOG_PATH, "r");
    if (f == nullptr) {
        buf[0] = '\0';
        return ESP_ERR_NOT_FOUND;
    }

    /* Seek to end, scan backwards for max_lines newlines */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size <= 0) {
        fclose(f);
        buf[0] = '\0';
        return ESP_OK;
    }

    /* Limiter la zone de scan à buf_size pour éviter lecture mémoire excessive */
    long scan_size = (file_size < (long)buf_size) ? file_size : (long)buf_size;
    long start_pos = file_size - scan_size;

    fseek(f, start_pos, SEEK_SET);

    /* Lire le bloc en mémoire temporaire */
    size_t to_read = (size_t)scan_size;
    if (to_read >= buf_size) {
        to_read = buf_size - 1;
    }

    size_t nread = fread(buf, 1, to_read, f);
    fclose(f);
    buf[nread] = '\0';

    /* Trouver la position de la (max_lines)-ième ligne depuis la fin */
    int nl_count = 0;
    long cut_pos = (long)nread;

    /* Ignorer le dernier '\n' s'il termine le fichier */
    if (cut_pos > 0 && buf[cut_pos - 1] == '\n') {
        cut_pos--;
    }

    for (long i = cut_pos - 1; i >= 0; i--) {
        if (buf[i] == '\n') {
            nl_count++;
            if (nl_count >= max_lines) {
                /* Décaler le buffer pour ne garder que les dernières lignes */
                size_t offset = (size_t)(i + 1);
                size_t remaining = nread - offset;
                memmove(buf, buf + offset, remaining);
                buf[remaining] = '\0';
                return ESP_OK;
            }
        }
    }

    /* Moins de max_lines lignes dans le fichier — tout renvoyer */
    return ESP_OK;
}

/* ── Internal FAT partition ──────────────────────────────────────────── */

static const char *TAG_FAT = "FAT";
static bool s_fat_mounted = false;
static wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

esp_err_t bmu_fat_init(void)
{
    if (s_fat_mounted) return ESP_OK;

    const esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = true,
        .max_files = 4,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
    };

    esp_err_t ret = esp_vfs_fat_spiflash_mount_rw_wl(
        BMU_FAT_MOUNT, "fatfs", &mount_cfg, &s_wl_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_FAT, "FAT mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_fat_mounted = true;
    ESP_LOGI(TAG_FAT, "FAT interne montee sur %s", BMU_FAT_MOUNT);
    return ESP_OK;
}

bool bmu_fat_is_mounted(void)
{
    return s_fat_mounted;
}

/* ── NVS ─────────────────────────────────────────────────────────────── */

esp_err_t bmu_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG_NVS, "NVS corrompu — effacement et ré-initialisation");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret == ESP_OK) {
        ESP_LOGI(TAG_NVS, "NVS initialisé");
    }
    return ret;
}

esp_err_t bmu_nvs_get_str(const char *key, char *buf, size_t len)
{
    if (key == nullptr || buf == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open("bmu", NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_NVS, "nvs_open(bmu) lecture échouée: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t required = len;
    ret = nvs_get_str(handle, key, buf, &required);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG_NVS, "Clé '%s' non trouvée: %s", key, esp_err_to_name(ret));
    }

    nvs_close(handle);
    return ret;
}

esp_err_t bmu_nvs_set_str(const char *key, const char *value)
{
    if (key == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open("bmu", NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_NVS, "nvs_open(bmu) écriture échouée: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(handle, key, value);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG_NVS, "Écriture clé '%s' échouée: %s", key, esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG_NVS, "Clé '%s' sauvegardée", key);
    }

    nvs_close(handle);
    return ret;
}
