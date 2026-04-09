/**
 * @file bmu_wifi.cpp
 * @brief Composant WiFi STA + AP fallback pour BMU.
 *
 * Utilise bmu_config pour SSID/password (NVS avec fallback Kconfig).
 * Si BMU_WIFI_MAX_RETRY == 0, la reconnexion est infinie.
 *
 * State machine :
 *   IDLE → CONNECTING → CONNECTED
 *                    ↘ AP_FALLBACK (après N échecs STA)
 *   AP_FALLBACK → CONNECTED (si STA reconnecte → AP coupé)
 *
 * La task wifi_supervisor_task gère les délais et décisions ;
 * les event handlers se contentent de poster des bits dans s_wifi_event_group.
 */

#include "bmu_wifi.h"
#include "bmu_config.h"

#include <cstring>
#include <cstdio>

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

static const char *TAG = "WIFI";

// ── Bits EventGroup ─────────────────────────────────────────────────────────
// Bits publics (état connexion)
#define WIFI_CONNECTED_BIT      BIT0
// Bits internes (signaux vers la supervisor task)
#define EVT_STA_DISCONNECTED    BIT1   // STA vient de se déconnecter
#define EVT_STA_GOT_IP          BIT2   // IP obtenue

#define ALL_EVT_BITS  (EVT_STA_DISCONNECTED | EVT_STA_GOT_IP)

// ── État global ──────────────────────────────────────────────────────────────
static EventGroupHandle_t  s_wifi_event_group  = nullptr;
static bmu_wifi_state_t    s_state             = BMU_WIFI_STATE_IDLE;
static int                 s_retry_count       = 0;  // tentatives STA totales
static int                 s_fail_streak       = 0;  // échecs consécutifs sans IP
static char                s_ip_addr[16]       = {0};
static char                s_ap_ssid[33]       = {0};
static bool                s_ap_active         = false;

// ── Helpers AP fallback ──────────────────────────────────────────────────────

/**
 * @brief Démarre le softAP en mode APSTA.
 *        SSID = CONFIG_BMU_WIFI_AP_SSID_PREFIX + 4 derniers hex de la MAC WiFi.
 */
static void try_start_ap_fallback(void)
{
#if CONFIG_BMU_WIFI_AP_FALLBACK_ENABLED
    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);

    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s%02X%02X",
             CONFIG_BMU_WIFI_AP_SSID_PREFIX, mac[4], mac[5]);

    wifi_config_t ap_cfg = {};
    std::strncpy(reinterpret_cast<char *>(ap_cfg.ap.ssid),
                 s_ap_ssid, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len       = static_cast<uint8_t>(std::strlen(s_ap_ssid));
    ap_cfg.ap.channel        = 1;
    ap_cfg.ap.authmode       = WIFI_AUTH_OPEN;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.beacon_interval = 100;
    ap_cfg.ap.pmf_cfg.required = false;

    /* IMPORTANT : creer le netif AP AVANT esp_wifi_set_mode(APSTA).
     * Sur ESP-IDF 5.4, set_mode declenche wifi_softap_start qui attend
     * le netif alloue — sinon LoadProhibited sur offset 0x2c. */
    if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") == nullptr) {
        esp_netif_create_default_wifi_ap();
    }

    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "APSTA set_mode failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "AP set_config failed: %s", esp_err_to_name(ret));
        return;
    }

    s_ap_active = true;
    s_state     = BMU_WIFI_STATE_AP_FALLBACK;
    ESP_LOGW(TAG, "AP fallback actif — SSID : %s", s_ap_ssid);
#endif
}

/**
 * @brief Désactive le softAP et repasse en mode STA pur.
 */
static void stop_ap_fallback(void)
{
#if CONFIG_BMU_WIFI_AP_FALLBACK_ENABLED
    if (!s_ap_active) {
        return;
    }
    esp_err_t ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Retour STA set_mode failed: %s", esp_err_to_name(ret));
        return;
    }
    s_ap_active = false;
    s_ap_ssid[0] = '\0';
    ESP_LOGI(TAG, "AP fallback désactivé — STA reconnecté");
#endif
}

// ── Task superviseur ─────────────────────────────────────────────────────────

/**
 * @brief Task WiFi supervisor.
 *
 * Attend les signaux de l'event handler (EVT_STA_DISCONNECTED / EVT_STA_GOT_IP),
 * gère les délais de reconnexion, incrémente fail_streak, décide du fallback AP.
 * Boucle infinie — ne termine jamais.
 */
static void wifi_supervisor_task(void *pvParameters)
{
    for (;;) {
        // Attente d'un événement (timeout infini)
        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_event_group,
            ALL_EVT_BITS,
            pdTRUE,   // clear on exit
            pdFALSE,  // any bit
            portMAX_DELAY);

        if (bits & EVT_STA_GOT_IP) {
            // Connexion établie
            bool was_fallback = s_ap_active;
            stop_ap_fallback();
            s_fail_streak = 0;
            s_state       = BMU_WIFI_STATE_CONNECTED;
            if (was_fallback) {
                ESP_LOGI(TAG, "STA reconnecté depuis fallback — IP : %s", s_ip_addr);
            }
            // Réinitialiser aussi le bit DISCONNECTED résiduel
            xEventGroupClearBits(s_wifi_event_group, EVT_STA_DISCONNECTED);
        }

        if (bits & EVT_STA_DISCONNECTED) {
            // Délai avant retry (hors handler, pas de blocking call dans ISR/handler)
            vTaskDelay(pdMS_TO_TICKS(2000));

            s_fail_streak++;
            s_retry_count++;

            int max_retry = CONFIG_BMU_WIFI_MAX_RETRY;
            bool should_retry = (max_retry == 0) || (s_retry_count <= max_retry);

            if (!should_retry) {
                ESP_LOGE(TAG, "Nombre max de tentatives atteint (%d) — abandon", max_retry);
                s_state = BMU_WIFI_STATE_AP_FALLBACK; // reste visible même sans retry
                if (!s_ap_active) {
                    try_start_ap_fallback();
                }
                // Pas de retry : on attend quand même les événements futurs
                continue;
            }

            ESP_LOGW(TAG, "Déconnecté — reconnexion dans 2 s (tentative %d%s, streak %d)",
                     s_retry_count,
                     max_retry == 0 ? "/∞" : "",
                     s_fail_streak);

            s_state = BMU_WIFI_STATE_CONNECTING;

#if CONFIG_BMU_WIFI_AP_FALLBACK_ENABLED
            if (!s_ap_active &&
                s_fail_streak >= CONFIG_BMU_WIFI_AP_FALLBACK_THRESHOLD) {
                try_start_ap_fallback();
            }
#endif
            esp_wifi_connect();
        }
    }
}

// ── Handlers d'événements ───────────────────────────────────────────────────

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA démarré, tentative de connexion…");
        s_state = BMU_WIFI_STATE_CONNECTING;
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        s_ip_addr[0] = '\0';
        // Signaler à la supervisor task (elle gère le délai et le retry)
        xEventGroupSetBits(s_wifi_event_group, EVT_STA_DISCONNECTED);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        snprintf(s_ip_addr, sizeof(s_ip_addr), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Connecté — IP : %s", s_ip_addr);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT | EVT_STA_GOT_IP);
    }
}

// ── API publique ────────────────────────────────────────────────────────────

esp_err_t bmu_wifi_init(void)
{
    // NVS déjà initialisé par bmu_nvs_init() dans main

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == nullptr) {
        ESP_LOGE(TAG, "Impossible de créer l'EventGroup");
        return ESP_ERR_NO_MEM;
    }

    // Pile réseau
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // Configuration WiFi — buffers réduits pour coex BLE
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.static_rx_buf_num  = 6;   // 10 → 6 (coex BLE, 128KB internal reserve)
    cfg.dynamic_rx_buf_num = 16;  // 32 → 16
    cfg.dynamic_tx_buf_num = 16;  // 32 → 16
    esp_err_t wifi_ret = esp_wifi_init(&cfg);
    if (wifi_ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi init failed: %s — running sans WiFi",
                 esp_err_to_name(wifi_ret));
        return wifi_ret;
    }

    // Enregistrement des handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    // Configuration STA — SSID/password depuis bmu_config (NVS ou Kconfig)
    const char *ssid = bmu_config_get_wifi_ssid();
    const char *pass = bmu_config_get_wifi_password();

    wifi_config_t wifi_config = {};
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid),
                 ssid, sizeof(wifi_config.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char *>(wifi_config.sta.password),
                 pass, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode =
        (std::strlen(pass) > 0) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;  /* Support WPA3 transition */

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Créer la task superviseur (boucle infinie, prio 2, stack 3KB)
    BaseType_t task_ret = xTaskCreate(
        wifi_supervisor_task,
        "wifi_supervisor",
        3072,
        nullptr,
        2,
        nullptr);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Impossible de créer wifi_supervisor_task");
        return ESP_ERR_NO_MEM;
    }

    s_state = BMU_WIFI_STATE_CONNECTING;

    /* NE PAS appeler esp_wifi_start() ici — bmu_wifi_start() le fait.
     * Cela permet d'init BLE entre wifi_init et wifi_start. */
    ESP_LOGI(TAG, "WiFi configuré — SSID : %s (radio pas encore active)", ssid);
    return ESP_OK;
}

esp_err_t bmu_wifi_start(void)
{
    esp_err_t ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WiFi radio active");
    }
    return ret;
}

bool bmu_wifi_is_connected(void)
{
    if (s_wifi_event_group == nullptr) {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

esp_err_t bmu_wifi_get_ip(char *buf, size_t len)
{
    if (buf == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!bmu_wifi_is_connected() || s_ip_addr[0] == '\0') {
        buf[0] = '\0';
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    std::strncpy(buf, s_ip_addr, len - 1);
    buf[len - 1] = '\0';
    return ESP_OK;
}

esp_err_t bmu_wifi_get_rssi(int8_t *rssi)
{
    if (!bmu_wifi_is_connected() || rssi == nullptr) {
        return ESP_ERR_WIFI_NOT_CONNECT;
    }
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret == ESP_OK) {
        *rssi = ap_info.rssi;
    }
    return ret;
}

bmu_wifi_state_t bmu_wifi_get_state(void)
{
    return s_state;
}

bool bmu_wifi_is_ap_active(void)
{
    return s_ap_active;
}

esp_err_t bmu_wifi_get_ap_ssid(char *buf, size_t len)
{
    if (buf == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ap_active || s_ap_ssid[0] == '\0') {
        buf[0] = '\0';
    } else {
        std::strncpy(buf, s_ap_ssid, len - 1);
        buf[len - 1] = '\0';
    }
    return ESP_OK;
}
