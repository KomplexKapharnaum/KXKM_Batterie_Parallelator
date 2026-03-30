/**
 * @file bmu_display.cpp
 * @brief LVGL v9 display driver for ESP32-S3-BOX-3 (ILI9342C + TT21100 touch).
 *
 * Handles SPI bus, LCD panel, touch controller, LVGL port initialization,
 * and backlight PWM with auto-dim. The actual UI screens (grid, detail,
 * system, alerts) are created by other files — this module only provides
 * hardware init and the LVGL rendering task.
 */

#include "bmu_display.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_touch_tt21100.h"
#include "esp_lvgl_port.h"
#include "driver/spi_master.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DISP";

/* ── Hardware pin definitions (BOX-3 internal) ────────────────────────── */
#define DISP_SPI_HOST       SPI2_HOST
#define DISP_GPIO_MOSI      GPIO_NUM_6
#define DISP_GPIO_SCLK      GPIO_NUM_7
#define DISP_GPIO_DC        GPIO_NUM_4
#define DISP_GPIO_CS        GPIO_NUM_5
#define DISP_GPIO_RST       GPIO_NUM_48
#define DISP_GPIO_BL        ((gpio_num_t)CONFIG_BMU_DISPLAY_BACKLIGHT_GPIO)
#define DISP_SPI_CLK_HZ     (40 * 1000 * 1000)  // 40 MHz

#define DISP_H_RES           320
#define DISP_V_RES           240

#define TOUCH_I2C_PORT       I2C_NUM_0
#define TOUCH_GPIO_INT       GPIO_NUM_3

/* ── LEDC backlight config ────────────────────────────────────────────── */
#define BL_LEDC_TIMER        LEDC_TIMER_0
#define BL_LEDC_CHANNEL      LEDC_CHANNEL_0
#define BL_LEDC_MODE         LEDC_LOW_SPEED_MODE
#define BL_DUTY_FULL         255
#define BL_DUTY_DIM          40

/* ── File-static state ────────────────────────────────────────────────── */
static bmu_display_ctx_t   *s_ctx          = NULL;
static lv_display_t        *s_disp         = NULL;
static lv_obj_t            *s_screen       = NULL;
static int64_t              s_last_touch_us = 0;
static bool                 s_dimmed        = false;
static bool                 s_update_req    = false;

/* ══════════════════════════════════════════════════════════════════════ */
/*  Backlight                                                            */
/* ══════════════════════════════════════════════════════════════════════ */

static esp_err_t backlight_init(void)
{
    ledc_timer_config_t bl_timer = {};
    bl_timer.speed_mode      = BL_LEDC_MODE;
    bl_timer.duty_resolution = LEDC_TIMER_8_BIT;
    bl_timer.timer_num       = BL_LEDC_TIMER;
    bl_timer.freq_hz         = 5000;
    bl_timer.clk_cfg         = LEDC_AUTO_CLK;

    esp_err_t ret = ledc_timer_config(&bl_timer);
    if (ret != ESP_OK) return ret;

    ledc_channel_config_t bl_channel = {};
    bl_channel.gpio_num   = DISP_GPIO_BL;
    bl_channel.speed_mode = BL_LEDC_MODE;
    bl_channel.channel    = BL_LEDC_CHANNEL;
    bl_channel.timer_sel  = BL_LEDC_TIMER;
    bl_channel.duty       = BL_DUTY_FULL;
    bl_channel.hpoint     = 0;

    ret = ledc_channel_config(&bl_channel);
    if (ret != ESP_OK) return ret;

    s_last_touch_us = esp_timer_get_time();
    s_dimmed = false;
    ESP_LOGI(TAG, "Backlight PWM initialisé sur GPIO%d", (int)DISP_GPIO_BL);
    return ESP_OK;
}

static void backlight_set_duty(uint32_t duty)
{
    ledc_set_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BL_LEDC_MODE, BL_LEDC_CHANNEL);
}

/**
 * @brief Called periodically to dim backlight after inactivity.
 */
static void backlight_check_dim(void)
{
    uint32_t timeout_s = CONFIG_BMU_DISPLAY_BL_DIM_TIMEOUT_S;
    if (timeout_s == 0) return;  // Auto-dim désactivé

    int64_t now = esp_timer_get_time();
    int64_t elapsed_us = now - s_last_touch_us;

    if (!s_dimmed && elapsed_us > (int64_t)timeout_s * 1000000LL) {
        backlight_set_duty(BL_DUTY_DIM);
        s_dimmed = true;
        ESP_LOGI(TAG, "Backlight atténué (inactivité %ds)", (int)timeout_s);
    }
}

/* ══════════════════════════════════════════════════════════════════════ */
/*  SPI Bus + ILI9342C Panel                                             */
/* ══════════════════════════════════════════════════════════════════════ */

static esp_err_t display_panel_init(esp_lcd_panel_io_handle_t *io_handle_out,
                                     esp_lcd_panel_handle_t    *panel_out)
{
    /* --- SPI bus --- */
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num   = DISP_GPIO_MOSI;
    bus_cfg.miso_io_num   = -1;
    bus_cfg.sclk_io_num   = DISP_GPIO_SCLK;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = DISP_H_RES * DISP_V_RES * 2;  // Plein écran RGB565

    esp_err_t ret = spi_bus_initialize(DISP_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec init SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }

    /* --- Panel IO (SPI) --- */
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num         = DISP_GPIO_DC;
    io_config.cs_gpio_num         = DISP_GPIO_CS;
    io_config.pclk_hz             = DISP_SPI_CLK_HZ;
    io_config.lcd_cmd_bits        = 8;
    io_config.lcd_param_bits      = 8;
    io_config.spi_mode            = 0;
    io_config.trans_queue_depth   = 10;

    ret = esp_lcd_new_panel_io_spi(DISP_SPI_HOST, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec panel IO SPI: %s", esp_err_to_name(ret));
        return ret;
    }

    /* --- ILI9342C panel (register-compatible with ILI9341) --- */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = DISP_GPIO_RST;
    panel_config.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.bits_per_pixel = 16;

    ret = esp_lcd_new_panel_ili9341(io_handle, &panel_config, &panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec création panel ILI9341/9342C: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    /* BOX-3 : l'écran est monté en paysage, rotation 90° */
    esp_lcd_panel_swap_xy(panel, true);
    esp_lcd_panel_mirror(panel, false, true);
    esp_lcd_panel_disp_on_off(panel, true);

    *io_handle_out = io_handle;
    *panel_out     = panel;
    ESP_LOGI(TAG, "Panel ILI9342C initialisé (%dx%d, SPI@%dMHz)",
             DISP_H_RES, DISP_V_RES, DISP_SPI_CLK_HZ / 1000000);
    return ESP_OK;
}

/* ══════════════════════════════════════════════════════════════════════ */
/*  TT21100 Touch                                                        */
/* ══════════════════════════════════════════════════════════════════════ */

static esp_err_t touch_init(esp_lcd_touch_handle_t *touch_out)
{
    esp_lcd_panel_io_handle_t tp_io = NULL;

    esp_lcd_panel_io_i2c_config_t tp_io_cfg =
        ESP_LCD_TOUCH_IO_I2C_TT21100_CONFIG();

    esp_err_t ret = esp_lcd_new_panel_io_i2c(TOUCH_I2C_PORT, &tp_io_cfg, &tp_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec panel IO I2C touch: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_lcd_touch_config_t touch_cfg = {};
    touch_cfg.x_max        = DISP_H_RES;
    touch_cfg.y_max        = DISP_V_RES;
    touch_cfg.rst_gpio_num = (gpio_num_t)-1;
    touch_cfg.int_gpio_num = TOUCH_GPIO_INT;
    touch_cfg.flags.swap_xy  = 1;
    touch_cfg.flags.mirror_y = 1;

    ret = esp_lcd_touch_new_i2c_tt21100(tp_io, &touch_cfg, touch_out);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec init TT21100 touch: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Touch TT21100 initialisé (I2C%d, INT=GPIO%d)",
             TOUCH_I2C_PORT, (int)TOUCH_GPIO_INT);
    return ESP_OK;
}

/* ══════════════════════════════════════════════════════════════════════ */
/*  LVGL Port Integration                                                */
/* ══════════════════════════════════════════════════════════════════════ */

/**
 * @brief Callback invoked by LVGL port on each touch read — used to
 *        reset the backlight dim timer.
 */
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (data->state == LV_INDEV_STATE_PRESSED) {
        s_last_touch_us = esp_timer_get_time();
        if (s_dimmed) {
            backlight_set_duty(BL_DUTY_FULL);
            s_dimmed = false;
            ESP_LOGD(TAG, "Backlight réveillé par toucher");
        }
    }
}

static esp_err_t lvgl_port_setup(esp_lcd_panel_io_handle_t io_handle,
                                  esp_lcd_panel_handle_t    panel_handle,
                                  esp_lcd_touch_handle_t    touch_handle)
{
    /* --- LVGL port init (crée la tâche LVGL) --- */
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_priority = 4;
    lvgl_cfg.task_stack    = 8192;

    esp_err_t ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Échec lvgl_port_init: %s", esp_err_to_name(ret));
        return ret;
    }

    /* --- Ajouter l'affichage --- */
    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle    = io_handle;
    disp_cfg.panel_handle = panel_handle;
    disp_cfg.buffer_size  = DISP_H_RES * DISP_V_RES * sizeof(lv_color_t);
    disp_cfg.double_buffer = true;
    disp_cfg.hres          = DISP_H_RES;
    disp_cfg.vres          = DISP_V_RES;
    disp_cfg.rotation.swap_xy  = false;
    disp_cfg.rotation.mirror_x = false;
    disp_cfg.rotation.mirror_y = false;

    s_disp = lvgl_port_add_disp(&disp_cfg);
    if (s_disp == NULL) {
        ESP_LOGE(TAG, "Échec lvgl_port_add_disp");
        return ESP_FAIL;
    }

    /* --- Ajouter le touch --- */
    lvgl_port_touch_cfg_t touch_cfg_lvgl = {};
    touch_cfg_lvgl.disp   = s_disp;
    touch_cfg_lvgl.handle = touch_handle;

    lv_indev_t *indev = lvgl_port_add_touch(&touch_cfg_lvgl);
    if (indev == NULL) {
        ESP_LOGW(TAG, "Touch ajouté mais indev NULL — touch callbacks indisponibles");
    } else {
        /* Enregistrer le callback pour le réveil du backlight */
        lv_indev_add_event_cb(indev, [](lv_event_t *e) {
            lv_indev_data_t *data = lv_indev_get_read_data(lv_event_get_indev(e));
            if (data && data->state == LV_INDEV_STATE_PRESSED) {
                bmu_display_wake();
            }
        }, LV_EVENT_PRESSING, NULL);
    }

    /* --- Thème sombre avec couleurs KXKM --- */
    if (lvgl_port_lock(0)) {
        lv_theme_t *theme = lv_theme_default_init(
            s_disp,
            lv_color_hex(0x2979FF),   // Bleu primaire
            lv_color_hex(0x00C853),   // Vert secondaire
            true,                      // Dark mode
            LV_FONT_DEFAULT
        );
        lv_disp_set_theme(s_disp, theme);

        /* Créer l'écran conteneur principal */
        s_screen = lv_obj_create(NULL);
        lv_obj_set_size(s_screen, DISP_H_RES, DISP_V_RES);
        lv_scr_load(s_screen);

        lvgl_port_unlock();
    }

    ESP_LOGI(TAG, "LVGL port initialisé — double buffer, tâche prio=%d, stack=%d",
             (int)lvgl_cfg.task_priority, (int)lvgl_cfg.task_stack);
    return ESP_OK;
}

/* ══════════════════════════════════════════════════════════════════════ */
/*  Tâche périodique : dim backlight + update flag                       */
/* ══════════════════════════════════════════════════════════════════════ */

static void display_periodic_cb(void *arg)
{
    (void)arg;
    backlight_check_dim();

    if (s_update_req) {
        s_update_req = false;
        if (s_disp && lvgl_port_lock(0)) {
            lv_obj_invalidate(lv_scr_act());
            lvgl_port_unlock();
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════ */
/*  API publique                                                         */
/* ══════════════════════════════════════════════════════════════════════ */

esp_err_t bmu_display_init(bmu_display_ctx_t *ctx)
{
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Contexte display NULL");
        return ESP_ERR_INVALID_ARG;
    }
    s_ctx = ctx;

    ESP_LOGI(TAG, "=== Initialisation affichage BMU (BOX-3) ===");

    /* 1. Backlight */
    esp_err_t ret = backlight_init();
    if (ret != ESP_OK) return ret;

    /* 2. SPI + ILI9342C */
    esp_lcd_panel_io_handle_t io_handle   = NULL;
    esp_lcd_panel_handle_t    panel_handle = NULL;
    ret = display_panel_init(&io_handle, &panel_handle);
    if (ret != ESP_OK) return ret;

    /* 3. Touch TT21100 */
    esp_lcd_touch_handle_t touch_handle = NULL;
    ret = touch_init(&touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Touch init échoué — affichage sans touch");
        /* On continue sans touch, ce n'est pas bloquant */
    }

    /* 4. LVGL port + thème */
    ret = lvgl_port_setup(io_handle, panel_handle, touch_handle);
    if (ret != ESP_OK) return ret;

    /* 5. Timer périodique pour auto-dim et update requests */
    const esp_timer_create_args_t timer_args = {
        .callback = display_periodic_cb,
        .arg      = NULL,
        .name     = "disp_periodic",
    };
    esp_timer_handle_t periodic_timer = NULL;
    ret = esp_timer_create(&timer_args, &periodic_timer);
    if (ret == ESP_OK) {
        esp_timer_start_periodic(periodic_timer,
                                 CONFIG_BMU_DISPLAY_REFRESH_MS * 1000ULL);
    }

    ESP_LOGI(TAG, "=== Affichage BMU prêt (refresh=%dms, dim=%ds) ===",
             CONFIG_BMU_DISPLAY_REFRESH_MS, CONFIG_BMU_DISPLAY_BL_DIM_TIMEOUT_S);
    return ESP_OK;
}

void bmu_display_request_update(void)
{
    s_update_req = true;
}

void bmu_display_wake(void)
{
    s_last_touch_us = esp_timer_get_time();
    if (s_dimmed) {
        backlight_set_duty(BL_DUTY_FULL);
        s_dimmed = false;
        ESP_LOGD(TAG, "Backlight réveillé");
    }
}

lv_obj_t *bmu_display_get_screen_container(void)
{
    return s_screen;
}
