/**
 * bmu_balancer — Soft-balancing par duty-cycling + mesure R_int opportuniste.
 *
 * Principe : les batteries dont la tension depasse V_moy + seuil sont
 * deconnectees periodiquement (duty reduit) pour reduire leur contribution
 * au courant flotte. Pendant les fenetres OFF, on mesure R_int gratis.
 */

#include "bmu_balancer.h"
#include "bmu_protection.h"
#include "bmu_rint.h"
#include "bmu_ina237.h"
#include "bmu_tca9535.h"
#include "esp_log.h"
#include <cmath>

static const char *TAG = "BALANCER";

#if !CONFIG_BMU_BALANCER_ENABLED

esp_err_t bmu_balancer_init(bmu_protection_ctx_t *) { return ESP_OK; }
int bmu_balancer_tick(void) { return 0; }
bool bmu_balancer_is_off(uint8_t) { return false; }
int bmu_balancer_get_duty_pct(uint8_t) { return 100; }

#else

static bmu_protection_ctx_t *s_prot = NULL;

/* Etat par batterie */
static struct {
    int  on_counter;      /* cycles ON restants avant OFF */
    int  off_counter;     /* cycles OFF restants avant ON */
    bool balancing;       /* true = cette batterie est duty-cycled */
    float v_before_mv;    /* V sous charge avant le OFF (pour R_int) */
    float i_before_a;     /* I sous charge avant le OFF (pour R_int) */
} s_bat[BMU_MAX_BATTERIES];

esp_err_t bmu_balancer_init(bmu_protection_ctx_t *prot)
{
    if (prot == NULL) return ESP_ERR_INVALID_ARG;
    s_prot = prot;
    for (int i = 0; i < BMU_MAX_BATTERIES; i++) {
        s_bat[i].on_counter  = CONFIG_BMU_BALANCE_DUTY_ON;
        s_bat[i].off_counter = 0;
        s_bat[i].balancing   = false;
    }
    ESP_LOGI(TAG, "Init OK — seuil=%d mV, duty ON=%d OFF=%d, min_conn=%d",
             CONFIG_BMU_BALANCE_HIGH_MV, CONFIG_BMU_BALANCE_DUTY_ON,
             CONFIG_BMU_BALANCE_DUTY_OFF, CONFIG_BMU_BALANCE_MIN_CONNECTED);
    return ESP_OK;
}

int bmu_balancer_tick(void)
{
    if (s_prot == NULL) return 0;

    int nb = s_prot->nb_ina;
    if (nb < CONFIG_BMU_BALANCE_MIN_CONNECTED) return 0;

    /* -- Calcul V_moy des batteries connectees ----------------------- */
    float sum_v = 0;
    int n_conn = 0;
    for (int i = 0; i < nb; i++) {
        if (bmu_protection_get_state(s_prot, i) != BMU_STATE_CONNECTED) continue;
        if (s_bat[i].off_counter > 0) continue; /* exclure les OFF du calcul */
        float v = bmu_protection_get_voltage(s_prot, i);
        if (v > 1000.0f) { /* > 1V = capteur valide */
            sum_v += v;
            n_conn++;
        }
    }

    if (n_conn < CONFIG_BMU_BALANCE_MIN_CONNECTED) return 0;
    float v_moy = sum_v / (float)n_conn;

    int balancing_count = 0;

    for (int i = 0; i < nb; i++) {
        bmu_battery_state_t state = bmu_protection_get_state(s_prot, i);

        /* Ne jamais toucher aux batteries non connectees */
        if (state != BMU_STATE_CONNECTED && s_bat[i].off_counter == 0) {
            s_bat[i].balancing = false;
            continue;
        }

        /* -- Batterie en phase OFF (duty reduit) --------------------- */
        if (s_bat[i].off_counter > 0) {
            s_bat[i].off_counter--;
            balancing_count++;

            if (s_bat[i].off_counter == 0) {
                /* Fin de la fenetre OFF -> reconnecter */
                int tca_idx = i / 4;
                int ch = i % 4;
                if (tca_idx < s_prot->nb_tca) {
                    bmu_tca9535_switch_battery(&s_prot->tca_devices[tca_idx], ch, true);
                    bmu_tca9535_set_led(&s_prot->tca_devices[tca_idx], ch, false, true);
                    ESP_LOGD(TAG, "BAT[%d] balance ON (fin duty OFF)", i + 1);
                }
                s_bat[i].on_counter = CONFIG_BMU_BALANCE_DUTY_ON;
            }
            continue;
        }

        /* -- Batterie en phase ON — verifier si duty-cycle necessaire - */
        float v = bmu_protection_get_voltage(s_prot, i);
        float delta = v - v_moy;

        if (delta > (float)CONFIG_BMU_BALANCE_HIGH_MV) {
            /* Tension trop haute -> decompter les cycles ON */
            s_bat[i].on_counter--;
            s_bat[i].balancing = true;

            if (s_bat[i].on_counter <= 0) {
                /* Temps de deconnecter pour reduire le duty */
                int tca_idx = i / 4;
                int ch = i % 4;
                if (tca_idx < s_prot->nb_tca) {
                    /* Lire V/I avant le OFF pour R_int */
                    float v1 = 0, i1 = 0;
                    bmu_ina237_read_voltage_current(
                        &s_prot->ina_devices[i], &v1, &i1);
                    s_bat[i].v_before_mv = v1;
                    s_bat[i].i_before_a = i1;

                    /* Switch OFF */
                    bmu_tca9535_switch_battery(&s_prot->tca_devices[tca_idx], ch, false);
                    bmu_tca9535_set_led(&s_prot->tca_devices[tca_idx], ch, true, false);

                    /* Declencher mesure R_int opportuniste */
                    bmu_rint_on_disconnect((uint8_t)i, v1, i1);

                    ESP_LOGI(TAG, "BAT[%d] balance OFF (V=%.0f > moy=%.0f +%d) R_int triggered",
                             i + 1, v, v_moy, CONFIG_BMU_BALANCE_HIGH_MV);
                }
                s_bat[i].off_counter = CONFIG_BMU_BALANCE_DUTY_OFF;
                balancing_count++;
            }
        } else {
            /* Tension OK — reset duty, pas de balancing */
            s_bat[i].on_counter = CONFIG_BMU_BALANCE_DUTY_ON;
            s_bat[i].balancing = false;
        }
    }

    return balancing_count;
}

bool bmu_balancer_is_off(uint8_t idx)
{
    if (idx >= BMU_MAX_BATTERIES) return false;
    return s_bat[idx].off_counter > 0;
}

int bmu_balancer_get_duty_pct(uint8_t idx)
{
    if (idx >= BMU_MAX_BATTERIES || !s_bat[idx].balancing) return 100;
    int total = CONFIG_BMU_BALANCE_DUTY_ON + CONFIG_BMU_BALANCE_DUTY_OFF;
    return (CONFIG_BMU_BALANCE_DUTY_ON * 100) / total;
}

#endif /* CONFIG_BMU_BALANCER_ENABLED */
