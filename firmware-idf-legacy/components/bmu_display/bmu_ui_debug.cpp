/**
 * @file bmu_ui_debug.cpp
 * @brief Ecran "Debug I2C" — log temps-reel du bus I2C + compteurs d'erreurs.
 *
 * Utile pour le debug terrain sans cable serie.
 * Ring buffer de 30 messages, affichage scrollable, couleurs par type.
 * Section supplementaire : resistance interne par batterie (si BMU_RINT_ENABLED).
 */

#include "bmu_ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>

#if CONFIG_BMU_RINT_ENABLED
#include "bmu_rint.h"
#endif

static const char *TAG = "UI_DBG";

/* ── Ring buffer pour les messages debug ─────────────────────────────── */
#define DEBUG_LOG_MAX 30
#define DEBUG_MSG_LEN 48

static char debug_log[DEBUG_LOG_MAX][DEBUG_MSG_LEN] = {};
static int debug_log_count = 0;
static int debug_log_head = 0; // prochaine position d'ecriture
static uint32_t error_count = 0;
static uint32_t nack_count = 0;
static uint32_t timeout_count = 0;
static int device_count = 0;

/* ── Widgets LVGL ────────────────────────────────────────────────────── */
static lv_obj_t *status_label = NULL;
static lv_obj_t *error_label = NULL;
static lv_obj_t *log_container = NULL;

#if CONFIG_BMU_RINT_ENABLED
static lv_obj_t *s_rint_labels[BMU_MAX_BATTERIES] = {};
#endif

/* ── API publique : ajouter un message (thread-safe via copie) ───────── */
static void debug_screen_log(const char *msg)
{
    if (msg == NULL) return;

    /* Horodatage depuis le boot */
    int64_t ms = esp_timer_get_time() / 1000;
    int sec = (int)(ms / 1000) % 86400;
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;

    snprintf(debug_log[debug_log_head], DEBUG_MSG_LEN,
             "%02d:%02d:%02d %s", h, m, s, msg);
    debug_log_head = (debug_log_head + 1) % DEBUG_LOG_MAX;
    if (debug_log_count < DEBUG_LOG_MAX) debug_log_count++;
}

[[maybe_unused]] static void debug_screen_log_i2c_error(uint8_t addr, const char *type)
{
    char buf[40];
    snprintf(buf, sizeof(buf), "%s 0x%02X", type, addr);
    debug_screen_log(buf);

    if (strcmp(type, "NACK") == 0) nack_count++;
    else if (strcmp(type, "TIMEOUT") == 0) timeout_count++;
    error_count++;
}

[[maybe_unused]] static void debug_screen_set_device_count(int count)
{
    device_count = count;
}

/* ── Creation de l'ecran ─────────────────────────────────────────────── */
void bmu_ui_debug_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x1E1E1E), 0);

    /* Titre */
    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text(title, "Debug I2C");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFF9100), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 8, 4);

    /* Bouton CLEAR */
    lv_obj_t *btn_clear = lv_button_create(parent);
    lv_obj_set_size(btn_clear, 60, 22);
    lv_obj_align(btn_clear, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(btn_clear, lv_color_hex(0x424242), 0);
    lv_obj_t *btn_lbl = lv_label_create(btn_clear);
    lv_label_set_text(btn_lbl, "CLEAR");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_lbl);
    lv_obj_add_event_cb(btn_clear, [](lv_event_t *e) {
        (void)e;
        debug_log_count = 0;
        debug_log_head = 0;
        error_count = 0;
        nack_count = 0;
        timeout_count = 0;
        memset(debug_log, 0, sizeof(debug_log));
        ESP_LOGI(TAG, "Debug log efface");
    }, LV_EVENT_CLICKED, NULL);

    /* Ligne de statut : info bus + nb devices */
    status_label = lv_label_create(parent);
    lv_label_set_text(status_label, "Bus: GPIO40/41 50kHz  Devices: 0");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0x9E9E9E), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 8, 26);

    /* Compteurs d'erreurs */
    error_label = lv_label_create(parent);
    lv_label_set_text(error_label, "Errors: 0  NACK: 0  Timeout: 0");
    lv_obj_set_style_text_color(error_label, lv_color_hex(0xFF1744), 0);
    lv_obj_set_style_text_font(error_label, &lv_font_montserrat_14, 0);
    lv_obj_align(error_label, LV_ALIGN_TOP_LEFT, 8, 40);

    /* Conteneur scrollable pour le log */
    log_container = lv_obj_create(parent);
    lv_obj_set_size(log_container, 310, 180);
    lv_obj_align(log_container, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(log_container, lv_color_hex(0x121212), 0);
    lv_obj_set_style_border_width(log_container, 0, 0);
    lv_obj_set_style_radius(log_container, 4, 0);
    lv_obj_set_style_pad_all(log_container, 4, 0);
    lv_obj_set_flex_flow(log_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(log_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(log_container, LV_DIR_VER);
}

#if CONFIG_BMU_RINT_ENABLED
/* ── Section R_int : creation ────────────────────────────────────────── */
void bmu_ui_debug_create_rint_section(lv_obj_t *parent, uint8_t nb_ina)
{
    if (nb_ina == 0 || nb_ina > BMU_MAX_BATTERIES) return;

    /* En-tete de section */
    lv_obj_t *hdr = lv_label_create(parent);
    lv_label_set_text(hdr, "Internal Resistance (mohm)");
    lv_obj_set_style_text_color(hdr, lv_color_hex(0xFF9100), 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_14, 0);

    /* Label par batterie */
    for (uint8_t i = 0; i < nb_ina; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "B%02u: --", (unsigned)(i + 1));
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x9E9E9E), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        s_rint_labels[i] = lbl;
    }
}

/* ── Section R_int : mise a jour ─────────────────────────────────────── */
void bmu_ui_debug_update_rint(uint8_t nb_ina)
{
    if (nb_ina == 0 || nb_ina > BMU_MAX_BATTERIES) return;

    for (uint8_t i = 0; i < nb_ina; i++) {
        if (s_rint_labels[i] == NULL) continue;

        bmu_rint_result_t r = bmu_rint_get_cached(i);
        char buf[32];

        if (!r.valid) {
            snprintf(buf, sizeof(buf), "B%02u: --", (unsigned)(i + 1));
            lv_label_set_text(s_rint_labels[i], buf);
            lv_obj_set_style_text_color(s_rint_labels[i], lv_color_hex(0x9E9E9E), 0);
        } else {
            snprintf(buf, sizeof(buf), "B%02u: R0=%.1f  Rt=%.1f",
                     (unsigned)(i + 1), r.r_ohmic_mohm, r.r_total_mohm);
            lv_label_set_text(s_rint_labels[i], buf);

            lv_color_t col;
            if (r.r_ohmic_mohm <= CONFIG_BMU_RINT_DISPLAY_WARN_MOHM) {
                col = lv_color_hex(0x66BB6A); /* vert */
            } else if (r.r_ohmic_mohm <= CONFIG_BMU_RINT_DISPLAY_CRIT_MOHM) {
                col = lv_color_hex(0xFFA726); /* orange */
            } else {
                col = lv_color_hex(0xFF4444); /* rouge */
            }
            lv_obj_set_style_text_color(s_rint_labels[i], col, 0);
        }
    }
}
#endif /* CONFIG_BMU_RINT_ENABLED */

/* ── Mise a jour periodique ──────────────────────────────────────────── */
void bmu_ui_debug_update(void)
{
    if (status_label == NULL) return;

    /* Mise a jour statut */
    char buf[48];
    snprintf(buf, sizeof(buf), "Bus: GPIO40/41 50kHz  Devices: %d", device_count);
    lv_label_set_text(status_label, buf);

    /* Mise a jour compteurs d'erreurs */
    snprintf(buf, sizeof(buf), "Errors: %lu  NACK: %lu  Timeout: %lu",
             (unsigned long)error_count, (unsigned long)nack_count, (unsigned long)timeout_count);
    lv_label_set_text(error_label, buf);

    /* Rafraichir le log (clear + rebuild — approche simple pour <30 items) */
    if (log_container == NULL) return;
    lv_obj_clean(log_container);

    /* Afficher du plus recent au plus ancien */
    for (int i = debug_log_count - 1; i >= 0; i--) {
        int idx = (debug_log_head - 1 - (debug_log_count - 1 - i) + DEBUG_LOG_MAX) % DEBUG_LOG_MAX;
        if (debug_log[idx][0] == '\0') continue;

        lv_obj_t *line = lv_label_create(log_container);
        lv_label_set_text(line, debug_log[idx]);

        /* Couleur selon le type : rouge erreurs, orange NACK, gris info */
        lv_color_t color = lv_color_hex(0x9E9E9E); /* gris par defaut */
        if (strstr(debug_log[idx], "NACK") != NULL) {
            color = lv_color_hex(0xFF9100); /* orange */
        } else if (strstr(debug_log[idx], "ERROR") != NULL || strstr(debug_log[idx], "FAIL") != NULL) {
            color = lv_color_hex(0xFF1744); /* rouge */
        } else if (strstr(debug_log[idx], "OK") != NULL) {
            color = lv_color_hex(0x00C853); /* vert */
        }
        lv_obj_set_style_text_color(line, color, 0);
        lv_obj_set_style_text_font(line, &lv_font_montserrat_14, 0);
    }

    /* Scroll vers le haut (plus recent) */
    lv_obj_scroll_to_y(log_container, 0, LV_ANIM_OFF);
}
