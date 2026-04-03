/**
 * @file bmu_rint_output.cpp
 * @brief Routage de sortie des mesures R_int — MQTT, NVS, formatage.
 *
 * Fonctions publiques (linkées depuis bmu_rint.cpp) :
 *   rint_output_route()       — point d'entrée principal
 *   bmu_rint_nvs_load_cache() — chargement NVS au boot
 *
 * Format InfluxDB line protocol :
 *   rint,battery=<N>,trigger=<str> r_ohmic_mohm=<f>,... <timestamp_ms>
 *
 * Format JSON :
 *   {"index":<N>,"r_ohmic_mohm":<f>,...,"valid":<b>}
 *
 * NVS namespace "bmu_rint", clés "rint_0".."rint_N" (blob bmu_rint_result_t).
 *
 * MQTT topic : bmu/rint/<idx>, payload InfluxDB line format.
 */

#include "bmu_rint.h"
#include "bmu_mqtt.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "RINT_OUT";

/* ── Constantes ─────────────────────────────────────────────────────────── */
#define NVS_NS_RINT     "bmu_rint"
#define NVS_KEY_FMT     "rint_%u"
#define NVS_KEY_MAX_LEN 16   /* "rint_31\0" */

/* ── Noms des triggers ───────────────────────────────────────────────────── */
static const char *trigger_str(bmu_rint_trigger_t t)
{
    switch (t) {
        case BMU_RINT_TRIGGER_OPPORTUNISTIC: return "opportunistic";
        case BMU_RINT_TRIGGER_PERIODIC:      return "periodic";
        case BMU_RINT_TRIGGER_ON_DEMAND:     return "on_demand";
        default:                             return "unknown";
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Formatage
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Formate une mesure en InfluxDB line protocol.
 *
 * Format : rint,battery=N,trigger=T r_ohmic_mohm=F,r_total_mohm=F,
 *          r_polar_mohm=F,v_load_mv=F,v_ocv_fast_mv=F,v_ocv_stable_mv=F,
 *          i_load_a=F <timestamp_ms>
 *
 * @param buf    Buffer de sortie.
 * @param bufsz  Taille du buffer.
 * @param idx    Index de la batterie.
 * @param t      Trigger de la mesure.
 * @param res    Résultat de mesure.
 * @return Nombre d'octets écrits (sans le \0), ou -1 si buffer trop petit.
 */
static int rint_format_influx(char *buf, size_t bufsz,
                               uint8_t idx, bmu_rint_trigger_t t,
                               const bmu_rint_result_t *res)
{
    float r_polar = res->r_total_mohm - res->r_ohmic_mohm;
    int n = snprintf(buf, bufsz,
        "rint,battery=%u,trigger=%s "
        "r_ohmic_mohm=%.2f,r_total_mohm=%.2f,r_polar_mohm=%.2f,"
        "v_load_mv=%.1f,v_ocv_fast_mv=%.1f,v_ocv_stable_mv=%.1f,"
        "i_load_a=%.4f "
        "%" PRId64,
        (unsigned)idx, trigger_str(t),
        res->r_ohmic_mohm, res->r_total_mohm, r_polar,
        res->v_load_mv, res->v_ocv_fast_mv, res->v_ocv_stable_mv,
        res->i_load_a,
        res->timestamp_ms);
    if (n < 0 || (size_t)n >= bufsz) {
        return -1;
    }
    return n;
}

/**
 * @brief Formate une mesure en JSON.
 *
 * @param buf    Buffer de sortie.
 * @param bufsz  Taille du buffer.
 * @param idx    Index de la batterie.
 * @param res    Résultat de mesure.
 * @return Nombre d'octets écrits (sans le \0), ou -1 si buffer trop petit.
 */
static int __attribute__((unused)) rint_format_json(char *buf, size_t bufsz,
                                                     uint8_t idx,
                                                     const bmu_rint_result_t *res)
{
    float r_polar = res->r_total_mohm - res->r_ohmic_mohm;
    int n = snprintf(buf, bufsz,
        "{\"index\":%u,"
        "\"r_ohmic_mohm\":%.2f,"
        "\"r_total_mohm\":%.2f,"
        "\"r_polar_mohm\":%.2f,"
        "\"v_load_mv\":%.1f,"
        "\"v_ocv_stable_mv\":%.1f,"
        "\"i_load_a\":%.4f,"
        "\"timestamp\":%" PRId64 ","
        "\"valid\":%s}",
        (unsigned)idx,
        res->r_ohmic_mohm,
        res->r_total_mohm,
        r_polar,
        res->v_load_mv,
        res->v_ocv_stable_mv,
        res->i_load_a,
        res->timestamp_ms,
        res->valid ? "true" : "false");
    if (n < 0 || (size_t)n >= bufsz) {
        return -1;
    }
    return n;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Persistance NVS
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Sauvegarde un résultat R_int en NVS (blob).
 *
 * Namespace "bmu_rint", clé "rint_N".
 *
 * @param idx    Index batterie.
 * @param result Résultat à sauvegarder.
 */
static void nvs_save_result(uint8_t idx, const bmu_rint_result_t *result)
{
    char key[NVS_KEY_MAX_LEN];
    snprintf(key, sizeof(key), NVS_KEY_FMT, (unsigned)idx);

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS_RINT, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS open '%s' failed: %s", NVS_NS_RINT, esp_err_to_name(ret));
        return;
    }

    ret = nvs_set_blob(h, key, result, sizeof(bmu_rint_result_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(h);
    }
    nvs_close(h);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS save '%s' failed: %s", key, esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "NVS sauvegarde bat %u OK", (unsigned)idx);
    }
}

/**
 * @brief Charge tous les résultats R_int depuis NVS dans le cache fourni.
 *
 * Appelé au boot avant bmu_rint_init() pour restaurer les dernières mesures.
 *
 * @param cache Tableau de bmu_rint_result_t (taille nb).
 * @param nb    Nombre d'entrées dans le tableau (BMU_MAX_BATTERIES max).
 */
extern "C" void bmu_rint_nvs_load_cache(bmu_rint_result_t *cache, uint8_t nb)
{
    if (cache == NULL || nb == 0) return;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS_RINT, NVS_READONLY, &h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "NVS namespace '%s' vide — cache R_int reset", NVS_NS_RINT);
        return;
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NVS open '%s' failed: %s", NVS_NS_RINT, esp_err_to_name(ret));
        return;
    }

    int loaded = 0;
    for (uint8_t i = 0; i < nb; i++) {
        char key[NVS_KEY_MAX_LEN];
        snprintf(key, sizeof(key), NVS_KEY_FMT, (unsigned)i);

        size_t sz = sizeof(bmu_rint_result_t);
        esp_err_t r = nvs_get_blob(h, key, &cache[i], &sz);
        if (r == ESP_OK && sz == sizeof(bmu_rint_result_t)) {
            loaded++;
            ESP_LOGD(TAG, "NVS restore bat %u : R_ohmic=%.1f mΩ valid=%d",
                     (unsigned)i, cache[i].r_ohmic_mohm, (int)cache[i].valid);
        }
        /* Si absent ou taille incorrecte, on laisse l'entrée à zéro (initialisée par memset) */
    }

    nvs_close(h);
    ESP_LOGI(TAG, "NVS R_int : %d/%d entrées restaurées", loaded, (int)nb);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Publication MQTT
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Publie un résultat R_int sur le broker MQTT.
 *
 * Topic  : bmu/rint/<idx>
 * Payload: InfluxDB line protocol
 * QoS 0, non retenu.
 *
 * @param idx     Index batterie.
 * @param trigger Déclencheur de la mesure.
 * @param res     Résultat validé.
 */
static void mqtt_publish_rint(uint8_t idx, bmu_rint_trigger_t trigger,
                               const bmu_rint_result_t *res)
{
    if (!bmu_mqtt_is_connected()) {
        ESP_LOGD(TAG, "MQTT non connecté — publication R_int bat %u ignorée", (unsigned)idx);
        return;
    }

    /* Payload InfluxDB line protocol (256 octets suffisent largement) */
    char payload[256];
    int n = rint_format_influx(payload, sizeof(payload), idx, trigger, res);
    if (n < 0) {
        ESP_LOGW(TAG, "rint_format_influx : buffer trop petit (bat %u)", (unsigned)idx);
        return;
    }

    char topic[32];
    snprintf(topic, sizeof(topic), "bmu/rint/%u", (unsigned)idx);

    esp_err_t ret = bmu_mqtt_publish(topic, payload, 0, 0, false);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "MQTT publish '%s' failed: %s", topic, esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "MQTT '%s' publié (%d octets)", topic, n);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Point d'entrée principal
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Route les sorties d'une mesure R_int selon le trigger.
 *
 * Règles :
 *   - Toujours   : log + MQTT (si valid && connecté)
 *   - Periodic / On-demand : NVS save (si valid)
 *   - On-demand  : display/web via polling bmu_rint_get_cached() — pas d'action ici
 *
 * @param idx     Index batterie (0..BMU_MAX_BATTERIES-1).
 * @param trigger Type de déclenchement.
 * @param res     Résultat (jamais NULL, appelant garanti).
 */
extern "C" void rint_output_route(uint8_t idx, bmu_rint_trigger_t trigger,
                                  const bmu_rint_result_t *res)
{
    if (res == NULL) return;

    ESP_LOGI(TAG, "Bat %u [%s] R_ohmic=%.1f mΩ R_total=%.1f mΩ valid=%d",
             (unsigned)idx, trigger_str(trigger),
             res->r_ohmic_mohm, res->r_total_mohm, (int)res->valid);

    /* ── MQTT : toujours, si mesure valide ───────────────────────────────── */
    if (res->valid) {
        mqtt_publish_rint(idx, trigger, res);
    }

    /* ── NVS : periodic + on_demand, si valide ───────────────────────────── */
    if (res->valid) {
        if (trigger == BMU_RINT_TRIGGER_PERIODIC ||
            trigger == BMU_RINT_TRIGGER_ON_DEMAND) {
            nvs_save_result(idx, res);
        }
    }

    /* ── On-demand display/web : polling via bmu_rint_get_cached() ─────── */
    /* Aucune action ici — le cache est mis à jour dans bmu_rint.cpp avant
     * l'appel à rint_output_route(). L'UI et le serveur web lisent le cache
     * via bmu_rint_get_cached(). */
}
