/*
 * bmu_web.cpp — Serveur HTTP + WebSocket pour BMU ESP-IDF
 *
 * Routes :
 *   GET  /               → index.html (SPIFFS)
 *   GET  /style.css      → style.css (SPIFFS)
 *   GET  /script.js      → script.js (SPIFFS)
 *   GET  /api/batteries  → JSON état batteries
 *   POST /api/battery/switch_on   → allumer batterie (auth + rate limit)
 *   POST /api/battery/switch_off  → éteindre batterie (auth + rate limit)
 *   GET  /api/log        → dernières lignes du log SD (chunked)
 *   GET  /ws             → WebSocket upgrade (push état 500ms)
 */

#include "bmu_web.h"
#include "bmu_web_security.h"
#include "bmu_protection.h"
#include "bmu_battery_manager.h"
#include "bmu_config.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_spiffs.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <inttypes.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

static const char *TAG = "WEB";

/* -------------------------------------------------------------------------- */
/*  État global du module                                                     */
/* -------------------------------------------------------------------------- */

static httpd_handle_t   s_server   = nullptr;
static bmu_web_ctx_t   *s_ctx      = nullptr;
static TaskHandle_t     s_ws_task  = nullptr;
static int              s_ws_fd    = -1;       /* fd du client WS connecté */

/* -------------------------------------------------------------------------- */
/*  Utilitaires                                                               */
/* -------------------------------------------------------------------------- */

static const char *state_to_string(bmu_battery_state_t st)
{
    switch (st) {
    case BMU_STATE_CONNECTED:    return "connected";
    case BMU_STATE_DISCONNECTED: return "disconnected";
    case BMU_STATE_RECONNECTING: return "reconnecting";
    case BMU_STATE_ERROR:        return "error";
    case BMU_STATE_LOCKED:       return "locked";
    default:                     return "unknown";
    }
}

/** Lit l'IP source depuis le socket fd du serveur httpd. */
static uint32_t get_client_ip(httpd_req_t *req)
{
    int sockfd = httpd_req_to_sockfd(req);
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) == 0) {
        return ntohl(addr.sin_addr.s_addr);
    }
    return 0;
}

/** Retourne le timestamp courant en ms. */
static int64_t now_ms(void)
{
    return (int64_t)(esp_timer_get_time() / 1000LL);
}

/* -------------------------------------------------------------------------- */
/*  SPIFFS file serving                                                       */
/* -------------------------------------------------------------------------- */

static esp_err_t serve_spiffs_file(httpd_req_t *req, const char *path,
                                    const char *content_type)
{
    FILE *f = fopen(path, "r");
    if (f == nullptr) {
        ESP_LOGE(TAG, "Fichier introuvable: %s", path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, content_type);

    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            httpd_resp_send_chunk(req, nullptr, 0);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, nullptr, 0);  /* fin du chunked transfer */
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/*  GET /                                                                     */
/* -------------------------------------------------------------------------- */

static esp_err_t handler_index(httpd_req_t *req)
{
    return serve_spiffs_file(req, "/spiffs/index.html", "text/html");
}

/* -------------------------------------------------------------------------- */
/*  GET /style.css                                                            */
/* -------------------------------------------------------------------------- */

static esp_err_t handler_style(httpd_req_t *req)
{
    return serve_spiffs_file(req, "/spiffs/style.css", "text/css");
}

/* -------------------------------------------------------------------------- */
/*  GET /script.js                                                            */
/* -------------------------------------------------------------------------- */

static esp_err_t handler_script(httpd_req_t *req)
{
    return serve_spiffs_file(req, "/spiffs/script.js", "application/javascript");
}

/* -------------------------------------------------------------------------- */
/*  GET /api/batteries                                                        */
/* -------------------------------------------------------------------------- */

static esp_err_t handler_api_batteries(httpd_req_t *req)
{
    if (s_ctx == nullptr || s_ctx->prot == nullptr || s_ctx->mgr == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No context");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *arr  = cJSON_AddArrayToObject(root, "batteries");

    const uint8_t nb = s_ctx->prot->nb_ina;
    for (int i = 0; i < nb; ++i) {
        cJSON *obj = cJSON_CreateObject();
        float current_a = 0.0f;
        if (i < s_ctx->mgr->nb_ina) {
            (void)bmu_ina237_read_current(&s_ctx->mgr->ina_devices[i], &current_a);
        }
        cJSON_AddNumberToObject(obj, "idx", i);
        cJSON_AddNumberToObject(obj, "voltage_mv",
            bmu_protection_get_voltage(s_ctx->prot, i));
        /* Courant : lecture directe depuis INA via battery manager */
        cJSON_AddNumberToObject(obj, "current_a", current_a);
        cJSON_AddStringToObject(obj, "state",
            state_to_string(bmu_protection_get_state(s_ctx->prot, i)));
        cJSON_AddNumberToObject(obj, "ah_discharge",
            bmu_battery_manager_get_ah_discharge(s_ctx->mgr, i));
        cJSON_AddItemToArray(arr, obj);
    }

    const char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free((void *)json);
    cJSON_Delete(root);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/*  Helpers mutation POST (switch_on / switch_off)                            */
/* -------------------------------------------------------------------------- */

/**
 * Parse le body JSON { "battery": N, "token": "..." }.
 * Retourne ESP_OK si parse réussie, sinon erreur + réponse HTTP envoyée.
 */
static esp_err_t parse_mutation_body(httpd_req_t *req, int *out_idx,
                                      char *out_token, size_t token_sz)
{
    /* Lecture du body (max 256 octets) */
    char body[256];
    int recv_len = httpd_req_recv(req, body, sizeof(body) - 1);
    if (recv_len <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
        return ESP_FAIL;
    }
    body[recv_len] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (root == nullptr) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *j_bat = cJSON_GetObjectItem(root, "battery");
    cJSON *j_tok = cJSON_GetObjectItem(root, "token");

    if (!cJSON_IsNumber(j_bat)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing battery index");
        return ESP_FAIL;
    }

    *out_idx = j_bat->valueint;

    if (j_tok != nullptr && cJSON_IsString(j_tok) && j_tok->valuestring != nullptr) {
        strncpy(out_token, j_tok->valuestring, token_sz - 1);
        out_token[token_sz - 1] = '\0';
    } else {
        out_token[0] = '\0';
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t handle_mutation(httpd_req_t *req, bool switch_on)
{
    const char *route = switch_on ? "/api/battery/switch_on"
                                  : "/api/battery/switch_off";

    /* 1. Token activé ? */
    if (!bmu_web_token_enabled()) {
        ESP_LOGW(TAG, "MUTATION %s REJETÉ: mutations désactivées (token vide)", route);
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "{\"error\":\"mutations disabled\"}");
        return ESP_OK;
    }

    /* 2. Rate limit */
    uint32_t ip = get_client_ip(req);
    if (bmu_web_rate_check(ip, now_ms())) {
        ESP_LOGW(TAG, "MUTATION %s REJETÉ: rate limit (IP 0x%08" PRIx32 ")",
                 route, ip);
        httpd_resp_set_status(req, "429 Too Many Requests");
        httpd_resp_sendstr(req, "{\"error\":\"rate limit exceeded\"}");
        return ESP_OK;
    }

    /* 3. Parse body */
    int battery_idx = -1;
    char token[128] = {0};
    if (parse_mutation_body(req, &battery_idx, token, sizeof(token)) != ESP_OK) {
        return ESP_OK; /* réponse erreur déjà envoyée */
    }

    /* 4. Vérif token */
    if (!bmu_web_token_valid(token)) {
        ESP_LOGW(TAG, "MUTATION %s bat=%d REJETÉ: token invalide (IP 0x%08" PRIx32 ")",
                 route, battery_idx, ip);
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "{\"error\":\"invalid token\"}");
        return ESP_OK;
    }

    /* 5. Bounds check */
    if (s_ctx == nullptr || s_ctx->prot == nullptr) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No context");
        return ESP_FAIL;
    }
    if (battery_idx < 0 || battery_idx >= s_ctx->prot->nb_ina) {
        ESP_LOGW(TAG, "MUTATION %s bat=%d REJETÉ: index hors bornes", route, battery_idx);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":\"battery index out of range\"}");
        return ESP_OK;
    }

    /* 6–7. Route through protection state machine (audit H-06) */
    esp_err_t ret = bmu_protection_web_switch(s_ctx->prot, battery_idx, switch_on);

    if (ret == ESP_ERR_NOT_ALLOWED) {
        ESP_LOGW(TAG, "MUTATION %s bat=%d REJETÉ: batterie verrouillée", route, battery_idx);
        httpd_resp_set_status(req, "423 Locked");
        httpd_resp_sendstr(req, "{\"error\":\"battery locked\"}");
        return ESP_OK;
    }
    if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "MUTATION %s bat=%d REJETÉ: tension hors limites", route, battery_idx);
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "{\"error\":\"voltage out of safe range\"}");
        return ESP_OK;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MUTATION %s bat=%d ERREUR: switch failed (%s)",
                 route, battery_idx, esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Switch failed");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "MUTATION %s bat=%d OK (IP 0x%08" PRIx32 ")",
             route, battery_idx, ip);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t handler_switch_on(httpd_req_t *req)
{
    return handle_mutation(req, true);
}

static esp_err_t handler_switch_off(httpd_req_t *req)
{
    return handle_mutation(req, false);
}

/* -------------------------------------------------------------------------- */
/*  GET /api/log — dernières 50 lignes du log SD (chunked)                    */
/* -------------------------------------------------------------------------- */

static esp_err_t handler_api_log(httpd_req_t *req)
{
    /*
     * Lit le fichier log depuis la SD et renvoie les 50 dernières lignes.
     * On lit le fichier entier par chunks pour trouver les lignes de fin.
     * Stratégie : seek vers la fin, lire en arrière pour trouver 50 newlines.
     */
    const char *log_path = "/sdcard/bmu.log";
    FILE *f = fopen(log_path, "r");
    if (f == nullptr) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"lines\":[],\"error\":\"log not available\"}");
        return ESP_OK;
    }

    /* Taille du fichier */
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);

    /* On lit au max les 8 derniers KB (suffisant pour ~50 lignes) */
    const long tail_size = 8192;
    long offset = (file_size > tail_size) ? (file_size - tail_size) : 0;
    long read_size = file_size - offset;

    char *buf = (char *)malloc(read_size + 1);
    if (buf == nullptr) {
        fclose(f);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    fseek(f, offset, SEEK_SET);
    size_t n = fread(buf, 1, read_size, f);
    fclose(f);
    buf[n] = '\0';

    /* Trouver les 50 dernières lignes */
    const int max_lines = 50;
    const char *line_starts[max_lines + 1];
    int line_count = 0;

    /* Scan arrière pour trouver les débuts de ligne */
    const char *p = buf + n;
    /* Ignorer trailing newline */
    if (p > buf && *(p - 1) == '\n') p--;

    while (p > buf && line_count < max_lines) {
        p--;
        if (*p == '\n') {
            line_starts[line_count++] = p + 1;
        }
    }
    if (p == buf && line_count < max_lines) {
        line_starts[line_count++] = buf;
    }

    /* Construire la réponse JSON en chunked */
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send_chunk(req, "{\"lines\":[", 10);

    for (int i = line_count - 1; i >= 0; --i) {
        const char *start = line_starts[i];
        /* Trouver la fin de cette ligne */
        const char *end = start;
        while (*end != '\0' && *end != '\n') end++;

        /* Échapper pour JSON (simple : on encadre avec quotes, pas d'escape complet) */
        httpd_resp_send_chunk(req, "\"", 1);
        /* Envoyer la ligne par morceaux pour éviter les " et \ non-échappés */
        for (const char *c = start; c < end; ++c) {
            if (*c == '"' || *c == '\\') {
                httpd_resp_send_chunk(req, "\\", 1);
            }
            httpd_resp_send_chunk(req, c, 1);
        }
        httpd_resp_send_chunk(req, "\"", 1);

        if (i > 0) {
            httpd_resp_send_chunk(req, ",", 1);
        }
    }

    httpd_resp_send_chunk(req, "]}", 2);
    httpd_resp_send_chunk(req, nullptr, 0);

    free(buf);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/*  WebSocket — GET /ws                                                       */
/* -------------------------------------------------------------------------- */

#if CONFIG_HTTPD_WS_SUPPORT
static esp_err_t handler_ws(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* Phase d'upgrade — on enregistre le fd */
        ESP_LOGI(TAG, "WS handshake depuis fd %d", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    /* Réception d'un frame WS */
    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(ws_pkt));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    /* Première lecture pour connaître la taille */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS recv_frame len failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (ws_pkt.len > 0 && ws_pkt.len < 256) {
        char *buf = (char *)malloc(ws_pkt.len + 1);
        if (buf == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = (uint8_t *)buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            free(buf);
            return ret;
        }
        buf[ws_pkt.len] = '\0';

        /* Le premier message texte doit contenir le token d'auth */
        if (!bmu_web_token_valid(buf)) {
            ESP_LOGW(TAG, "WS auth échouée fd=%d", httpd_req_to_sockfd(req));
            /* Envoyer un close frame */
            httpd_ws_frame_t close_pkt;
            memset(&close_pkt, 0, sizeof(close_pkt));
            close_pkt.type = HTTPD_WS_TYPE_CLOSE;
            httpd_ws_send_frame(req, &close_pkt);
            free(buf);
            return ESP_OK;
        }

        /* Auth OK — enregistrer ce fd pour le push périodique */
        s_ws_fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "WS authentifié fd=%d", s_ws_fd);

        /* Réponse de confirmation */
        httpd_ws_frame_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.type = HTTPD_WS_TYPE_TEXT;
        const char *msg = "{\"auth\":\"ok\"}";
        ack.payload = (uint8_t *)msg;
        ack.len = strlen(msg);
        httpd_ws_send_frame(req, &ack);

        free(buf);
    }

    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/*  Tâche FreeRTOS : push WS toutes les 500ms                                */
/* -------------------------------------------------------------------------- */

/** Contexte pour httpd_queue_work callback */
typedef struct {
    httpd_handle_t server;
    int            fd;
    char          *json;
} ws_push_ctx_t;

static void ws_send_work(void *arg)
{
    ws_push_ctx_t *push = (ws_push_ctx_t *)arg;
    if (push == nullptr) return;

    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.type    = HTTPD_WS_TYPE_TEXT;
    pkt.payload = (uint8_t *)push->json;
    pkt.len     = strlen(push->json);

    esp_err_t ret = httpd_ws_send_frame_async(push->server, push->fd, &pkt);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WS push échoué fd=%d: %s", push->fd, esp_err_to_name(ret));
        /* Déconnecter ce client */
        s_ws_fd = -1;
    }

    free(push->json);
    free(push);
}

static void ws_push_task(void *arg)
{
    (void)arg;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(500));

        if (s_ws_fd < 0 || s_server == nullptr || s_ctx == nullptr) {
            continue;
        }

        /* Construire le JSON état batteries */
        cJSON *root = cJSON_CreateObject();
        cJSON *arr  = cJSON_AddArrayToObject(root, "batteries");

        const uint8_t nb = s_ctx->prot->nb_ina;
        for (int i = 0; i < nb; ++i) {
            cJSON *obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(obj, "idx", i);
            cJSON_AddNumberToObject(obj, "voltage_mv",
                bmu_protection_get_voltage(s_ctx->prot, i));
            cJSON_AddStringToObject(obj, "state",
                state_to_string(bmu_protection_get_state(s_ctx->prot, i)));
            cJSON_AddNumberToObject(obj, "ah_discharge",
                bmu_battery_manager_get_ah_discharge(s_ctx->mgr, i));
            cJSON_AddItemToArray(arr, obj);
        }

        char *json = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (json == nullptr) {
            continue;
        }

        /* Envoyer via httpd_queue_work pour thread safety */
        ws_push_ctx_t *push = (ws_push_ctx_t *)malloc(sizeof(ws_push_ctx_t));
        if (push == nullptr) {
            free(json);
            continue;
        }
        push->server = s_server;
        push->fd     = s_ws_fd;
        push->json   = json;

        if (httpd_queue_work(s_server, ws_send_work, push) != ESP_OK) {
            free(json);
            free(push);
        }
    }
}
#endif

/* -------------------------------------------------------------------------- */
/*  Start / Stop                                                              */
/* -------------------------------------------------------------------------- */

esp_err_t bmu_web_start(bmu_web_ctx_t *ctx)
{
    if (ctx == nullptr || ctx->prot == nullptr || ctx->mgr == nullptr) {
        ESP_LOGE(TAG, "bmu_web_start: contexte invalide");
        return ESP_ERR_INVALID_ARG;
    }

    s_ctx = ctx;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers  = 10;
    config.uri_match_fn      = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* --- Enregistrement des routes --- */

    /* GET / */
    const httpd_uri_t uri_index = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = handler_index,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_index);

    /* GET /style.css */
    const httpd_uri_t uri_style = {
        .uri      = "/style.css",
        .method   = HTTP_GET,
        .handler  = handler_style,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_style);

    /* GET /script.js */
    const httpd_uri_t uri_script = {
        .uri      = "/script.js",
        .method   = HTTP_GET,
        .handler  = handler_script,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_script);

    /* GET /api/batteries */
    const httpd_uri_t uri_batteries = {
        .uri      = "/api/batteries",
        .method   = HTTP_GET,
        .handler  = handler_api_batteries,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_batteries);

    /* POST /api/battery/switch_on */
    const httpd_uri_t uri_switch_on = {
        .uri      = "/api/battery/switch_on",
        .method   = HTTP_POST,
        .handler  = handler_switch_on,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_switch_on);

    /* POST /api/battery/switch_off */
    const httpd_uri_t uri_switch_off = {
        .uri      = "/api/battery/switch_off",
        .method   = HTTP_POST,
        .handler  = handler_switch_off,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_switch_off);

    /* GET /api/log */
    const httpd_uri_t uri_log = {
        .uri      = "/api/log",
        .method   = HTTP_GET,
        .handler  = handler_api_log,
        .user_ctx = nullptr
    };
    httpd_register_uri_handler(s_server, &uri_log);

    #if CONFIG_HTTPD_WS_SUPPORT
    /* GET /ws — WebSocket */
    const httpd_uri_t uri_ws = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = handler_ws,
        .user_ctx     = nullptr,
        .is_websocket = true
    };
    httpd_register_uri_handler(s_server, &uri_ws);

    /* Tâche push WS */
    BaseType_t xret = xTaskCreate(ws_push_task, "ws_push", 4096, nullptr, 3, &s_ws_task);
    if (xret != pdPASS) {
        ESP_LOGW(TAG, "Impossible de créer la tâche ws_push");
    }
    #else
    ESP_LOGW(TAG, "WebSocket désactivé (CONFIG_HTTPD_WS_SUPPORT=0)");
    #endif

    ESP_LOGI(TAG, "Serveur HTTP démarré sur port %d", config.server_port);
    return ESP_OK;
}

esp_err_t bmu_web_stop(void)
{
    if (s_ws_task != nullptr) {
        vTaskDelete(s_ws_task);
        s_ws_task = nullptr;
    }

    s_ws_fd = -1;

    if (s_server != nullptr) {
        httpd_stop(s_server);
        s_server = nullptr;
    }

    s_ctx = nullptr;
    ESP_LOGI(TAG, "Serveur HTTP arrêté");
    return ESP_OK;
}
