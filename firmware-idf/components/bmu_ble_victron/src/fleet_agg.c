// firmware-idf-v2/components/bmu_ble_victron/src/fleet_agg.c
//
// Phase 21 : aggregation flotte — somme courants, moyenne tensions, max temp.

#include "fleet_agg.h"
#include "bmu_core.h"
#include <string.h>

void fleet_agg_compute(const struct BmuSnapshotC *snap, bmu_fleet_agg_t *out) {
    memset(out, 0, sizeof(*out));
    int64_t v_sum = 0;
    int64_t i_sum = 0;
    uint32_t soh_sum = 0;
    int32_t ah_sum = 0;
    int16_t t_max_c10 = -1280;  // -128.0 C
    uint8_t n = 0;

    for (int i = 0; i < snap->n_bat && i < MAX_BATTERIES; i++) {
        const struct BmuBatteryC *b = &snap->batteries[i];
        if (b->state != 1) continue;  // Online only
        v_sum += b->voltage_mv;
        i_sum += b->current_ma;
        soh_sum += (b->soh_pct != 0xFF) ? b->soh_pct : 50;  // default 50 if unknown
        ah_sum += b->ah_remaining_ma_h;
        if (b->temp_c10 > t_max_c10) t_max_c10 = b->temp_c10;
        n++;
    }

    if (n > 0) {
        out->voltage_mv = (uint16_t)(v_sum / n);
        out->current_ma = (int32_t)i_sum;
        out->soc_pct = (uint8_t)(soh_sum / n);
        out->power_cw = (int32_t)((int64_t)out->voltage_mv * i_sum / 100000);
        out->consumed_ah_mah = ah_sum;
        out->temp_max_c = (int8_t)(t_max_c10 / 10);
        out->n_online = n;
    }
}
