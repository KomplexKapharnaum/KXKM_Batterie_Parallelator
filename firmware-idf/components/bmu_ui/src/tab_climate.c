// firmware-idf-v2/components/bmu_ui/src/tab_climate.c
//
// Phase 17 -- onglet CLIM : temperature + humidite AHT30.

#include <stdlib.h>
#include <string.h>

#include "bmu_ui_internal.h"
#include "bmu_climate.h"

static lv_obj_t *s_lbl_climate = NULL;

void tab_climate_create(lv_obj_t *parent)
{
    lv_obj_set_scrollbar_mode(parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(parent, 4, LV_PART_MAIN);

    s_lbl_climate = lv_label_create(parent);
    lv_label_set_text(s_lbl_climate, "CLIMATE: no data");
    lv_obj_set_style_text_font(s_lbl_climate, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_lbl_climate, lv_color_hex(0xcccccc), LV_PART_MAIN);
    lv_obj_align(s_lbl_climate, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_width(s_lbl_climate, 310);
}

void tab_climate_update(void)
{
    if (!s_lbl_climate) return;

    bmu_climate_stats_t st;
    memset(&st, 0, sizeof(st));

    if (bmu_climate_get_stats(&st) != ESP_OK || !st.valid) {
        lv_label_set_text(s_lbl_climate, "CLIMATE: no data");
        return;
    }

    // Temperature : temp_c10 / 10 avec 1 decimale (integer math)
    int t_avg = st.temp_c10_avg;
    int t_int = t_avg / 10;
    int t_dec = abs(t_avg % 10);
    // Signe pour temperature negative
    const char *t_sign = t_avg < 0 ? "-" : "";
    if (t_avg < 0) t_int = -t_int;

    int t_min_int = st.temp_c10_min / 10;
    int t_min_dec = abs(st.temp_c10_min % 10);
    const char *t_min_sign = st.temp_c10_min < 0 ? "-" : "";
    if (st.temp_c10_min < 0) t_min_int = -t_min_int;

    int t_max_int = st.temp_c10_max / 10;
    int t_max_dec = abs(st.temp_c10_max % 10);
    const char *t_max_sign = st.temp_c10_max < 0 ? "-" : "";
    if (st.temp_c10_max < 0) t_max_int = -t_max_int;

    // Humidite : rh_pct10 / 10
    int h_avg = st.rh_pct10_avg / 10;
    int h_min = st.rh_pct10_min / 10;
    int h_max = st.rh_pct10_max / 10;

    lv_label_set_text_fmt(s_lbl_climate,
        "Temp: %s%d.%d C\n"
        "  min %s%d.%d / max %s%d.%d\n"
        "\n"
        "Humidity: %d %%RH\n"
        "  min %d / max %d\n"
        "\n"
        "Samples: %d",
        t_sign, t_int, t_dec,
        t_min_sign, t_min_int, t_min_dec,
        t_max_sign, t_max_int, t_max_dec,
        h_avg,
        h_min, h_max,
        (int)st.n_samples
    );
}
