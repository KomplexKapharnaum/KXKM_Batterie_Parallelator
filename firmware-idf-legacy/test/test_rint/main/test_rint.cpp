/**
 * @file test_rint.cpp
 * @brief Tests unitaires résistance interne (R_int) — portée ESP-IDF (Unity)
 *
 * Logique pure, pas de hardware. Miroir de ce qui deviendra bmu_rint.
 *
 * Couverture :
 *   - Guards : nombre min de batteries connectées, états erreur/lock, concurrent
 *   - Calcul R_ohmic et R_total à partir de V/I mesurés
 *   - Formatage InfluxDB line protocol
 *   - Formatage JSON
 */

#include <unity.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>

// -- Types (miroir de bmu_rint.h) -----------------------------------------

typedef enum {
    BMU_RINT_TRIGGER_OPPORTUNISTIC,
    BMU_RINT_TRIGGER_PERIODIC,
    BMU_RINT_TRIGGER_ON_DEMAND
} bmu_rint_trigger_t;

typedef struct {
    float r_ohmic_mohm;   ///< Résistance ohmique (chute sous charge), mΩ
    float r_total_mohm;   ///< Résistance totale (incluant polarisation), mΩ
    float v_load_mv;      ///< Tension sous charge, mV
    float v_ocv_fast_mv;  ///< OCV mesurée rapide (quelques ms après coupure), mV
    float v_ocv_stable_mv;///< OCV mesurée stable (100ms+ après coupure), mV
    float i_load_a;       ///< Courant de charge au moment de la mesure, A
    int64_t timestamp_ms; ///< Horodatage de la mesure, ms epoch
    bool valid;           ///< true si la mesure est exploitable
} bmu_rint_result_t;

// -- Logique guards -------------------------------------------------------

/**
 * Vérifie que la mesure R_int peut être lancée.
 *
 * @param nb_connected       Nombre de batteries actuellement connectées
 * @param target_connected   true si la batterie cible est connectée
 * @param has_error          true si la batterie cible est en erreur
 * @param is_locked          true si la batterie cible est verrouillée
 * @param measurement_active true si une mesure R_int est déjà en cours
 * @return true si les conditions permettent la mesure
 */
static bool rint_guard_check(int nb_connected,
                              bool target_connected,
                              bool has_error,
                              bool is_locked,
                              bool measurement_active)
{
    if (nb_connected < 2)       return false;
    if (!target_connected)      return false;
    if (has_error)              return false;
    if (is_locked)              return false;
    if (measurement_active)     return false;
    return true;
}

// -- Logique de calcul ----------------------------------------------------

// Seuils
static const float RINT_MIN_CURRENT_A  = 0.5f;   ///< Courant minimal pour mesure valide, A
static const float RINT_MAX_R_MOHM     = 500.0f;  ///< Résistance max acceptable, mΩ

/**
 * Calcule R_ohmic et R_total à partir des mesures V/I.
 *
 * R_ohmic  = (V_ocv_fast  - V_load) / |I_load|  × 1000  [mΩ]
 * R_total  = (V_ocv_stable - V_load) / |I_load|  × 1000  [mΩ]
 *
 * Retourne un résultat invalide si :
 *   - |I_load| < RINT_MIN_CURRENT_A
 *   - dV_ohmic <= 0 (pas de chute de tension)
 *   - R_ohmic ou R_total > RINT_MAX_R_MOHM
 *   - R_total < R_ohmic (incohérence physique)
 *
 * @param v_load_mv      Tension sous charge (mV)
 * @param i_load_a       Courant de charge (A, peut être négatif → abs utilisé)
 * @param v_ocv_fast_mv  OCV rapide post-coupure (mV)
 * @param v_ocv_stable_mv OCV stable post-coupure (mV)
 * @param timestamp_ms   Horodatage de la mesure
 * @return bmu_rint_result_t avec valid=true si la mesure est exploitable
 */
static bmu_rint_result_t rint_compute(float v_load_mv,
                                       float i_load_a,
                                       float v_ocv_fast_mv,
                                       float v_ocv_stable_mv,
                                       int64_t timestamp_ms)
{
    bmu_rint_result_t res = {};
    res.v_load_mv       = v_load_mv;
    res.v_ocv_fast_mv   = v_ocv_fast_mv;
    res.v_ocv_stable_mv = v_ocv_stable_mv;
    res.i_load_a        = i_load_a;
    res.timestamp_ms    = timestamp_ms;
    res.valid           = false;

    float i_abs = fabsf(i_load_a);
    if (i_abs < RINT_MIN_CURRENT_A) return res;

    float dv_ohmic = v_ocv_fast_mv - v_load_mv;
    if (dv_ohmic <= 0.0f) return res;

    float r_ohmic = (dv_ohmic / i_abs);          // mΩ (mV / A = mΩ)
    if (r_ohmic > RINT_MAX_R_MOHM) return res;

    float dv_total = v_ocv_stable_mv - v_load_mv;
    float r_total  = (dv_total / i_abs);
    if (r_total > RINT_MAX_R_MOHM) return res;
    if (r_total < r_ohmic) return res;

    res.r_ohmic_mohm = r_ohmic;
    res.r_total_mohm = r_total;
    res.valid        = true;
    return res;
}

// -- Formatage InfluxDB line protocol -------------------------------------

/**
 * Formate un résultat R_int en InfluxDB line protocol.
 *
 * Format :
 *   rint,battery=<idx>,trigger=<trig> r_ohmic_mohm=<v>,r_total_mohm=<v>,
 *     r_polar_mohm=<v>,v_load_mv=<v>,v_ocv_fast_mv=<v>,v_ocv_stable_mv=<v>,
 *     i_load_a=<v> <timestamp_ns>
 *
 * @param buf       Buffer de sortie
 * @param len       Taille du buffer
 * @param res       Résultat R_int à formater
 * @param bat_idx   Index de la batterie (tag)
 * @param trigger   Type de déclenchement (tag)
 * @param ts_ns     Timestamp en nanosecondes (pour InfluxDB)
 */
static void rint_format_influx(char *buf, size_t len,
                                const bmu_rint_result_t *res,
                                int bat_idx,
                                bmu_rint_trigger_t trigger,
                                int64_t ts_ns)
{
    const char *trig_str = "opportunistic";
    if (trigger == BMU_RINT_TRIGGER_PERIODIC)   trig_str = "periodic";
    if (trigger == BMU_RINT_TRIGGER_ON_DEMAND)  trig_str = "on_demand";

    float r_polar = res->r_total_mohm - res->r_ohmic_mohm;

    snprintf(buf, len,
        "rint,battery=%d,trigger=%s "
        "r_ohmic_mohm=%.3f,r_total_mohm=%.3f,r_polar_mohm=%.3f,"
        "v_load_mv=%.1f,v_ocv_fast_mv=%.1f,v_ocv_stable_mv=%.1f,"
        "i_load_a=%.3f "
        "%lld",
        bat_idx, trig_str,
        res->r_ohmic_mohm, res->r_total_mohm, r_polar,
        res->v_load_mv, res->v_ocv_fast_mv, res->v_ocv_stable_mv,
        res->i_load_a,
        (long long)ts_ns);
}

// -- Formatage JSON -------------------------------------------------------

/**
 * Formate un résultat R_int en objet JSON.
 *
 * Format :
 *   {"index":<n>,"valid":<true|false>,"timestamp_ms":<ts>,
 *    "r_ohmic_mohm":<v>,"r_total_mohm":<v>,"r_polar_mohm":<v>,
 *    "v_load_mv":<v>,"v_ocv_fast_mv":<v>,"v_ocv_stable_mv":<v>,
 *    "i_load_a":<v>}
 *
 * @param buf     Buffer de sortie
 * @param len     Taille du buffer
 * @param res     Résultat R_int à formater
 * @param bat_idx Index de la batterie
 */
static void rint_format_json(char *buf, size_t len,
                              const bmu_rint_result_t *res,
                              int bat_idx)
{
    float r_polar = res->r_total_mohm - res->r_ohmic_mohm;

    snprintf(buf, len,
        "{\"index\":%d,\"valid\":%s,\"timestamp_ms\":%lld,"
        "\"r_ohmic_mohm\":%.3f,\"r_total_mohm\":%.3f,\"r_polar_mohm\":%.3f,"
        "\"v_load_mv\":%.1f,\"v_ocv_fast_mv\":%.1f,\"v_ocv_stable_mv\":%.1f,"
        "\"i_load_a\":%.3f}",
        bat_idx,
        res->valid ? "true" : "false",
        (long long)res->timestamp_ms,
        res->r_ohmic_mohm, res->r_total_mohm, r_polar,
        res->v_load_mv, res->v_ocv_fast_mv, res->v_ocv_stable_mv,
        res->i_load_a);
}

// == Tests : Guards =======================================================

void test_guard_normal_pass(void)
{
    // 4 batteries connectées, cible OK, pas d'erreur, pas de mesure en cours
    TEST_ASSERT_TRUE(rint_guard_check(4, true, false, false, false));
}

void test_guard_too_few_connected(void)
{
    // Aucune batterie connectée
    TEST_ASSERT_FALSE(rint_guard_check(0, true, false, false, false));
}

void test_guard_exactly_one_fail(void)
{
    // 1 seule batterie : pas assez pour couper et mesurer
    TEST_ASSERT_FALSE(rint_guard_check(1, true, false, false, false));
}

void test_guard_exactly_two_pass(void)
{
    // 2 batteries : minimum acceptable
    TEST_ASSERT_TRUE(rint_guard_check(2, true, false, false, false));
}

void test_guard_error_state_fail(void)
{
    // Batterie en erreur → refus
    TEST_ASSERT_FALSE(rint_guard_check(4, true, true, false, false));
}

void test_guard_target_disconnected_fail(void)
{
    // Cible déconnectée → refus
    TEST_ASSERT_FALSE(rint_guard_check(4, false, false, false, false));
}

void test_guard_measurement_active_fail(void)
{
    // Mesure déjà en cours → refus
    TEST_ASSERT_FALSE(rint_guard_check(4, true, false, false, true));
}

// == Tests : Calcul =======================================================

void test_compute_normal(void)
{
    // V_load=26000mV, I=3A, V_fast=26045mV, V_stable=26072mV
    // R_ohmic = (26045-26000)/3 = 45/3 = 15 mΩ
    // R_total = (26072-26000)/3 = 72/3 = 24 mΩ
    bmu_rint_result_t res = rint_compute(26000.0f, 3.0f, 26045.0f, 26072.0f, 1000000LL);
    TEST_ASSERT_TRUE(res.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 15.0f, res.r_ohmic_mohm);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 24.0f, res.r_total_mohm);
}

void test_compute_low_current_rejected(void)
{
    // 0.2A < 0.5A seuil → invalide
    bmu_rint_result_t res = rint_compute(26000.0f, 0.2f, 26045.0f, 26072.0f, 1000000LL);
    TEST_ASSERT_FALSE(res.valid);
}

void test_compute_negative_dv_rejected(void)
{
    // V_fast < V_load → dV négatif → invalide
    bmu_rint_result_t res = rint_compute(26050.0f, 3.0f, 26000.0f, 26072.0f, 1000000LL);
    TEST_ASSERT_FALSE(res.valid);
}

void test_compute_excessive_r_rejected(void)
{
    // R_ohmic = (27000-26000)/3 = 333 mΩ encore OK
    // R_ohmic = (27600-26000)/3 = 533 mΩ > 500 → invalide
    bmu_rint_result_t res = rint_compute(26000.0f, 3.0f, 27600.0f, 27700.0f, 1000000LL);
    TEST_ASSERT_FALSE(res.valid);
}

void test_compute_r_total_less_than_r_ohmic_rejected(void)
{
    // V_stable < V_fast → R_total < R_ohmic → incohérence → invalide
    bmu_rint_result_t res = rint_compute(26000.0f, 3.0f, 26072.0f, 26045.0f, 1000000LL);
    TEST_ASSERT_FALSE(res.valid);
}

void test_compute_negative_current_uses_abs(void)
{
    // Courant négatif (décharge) : abs utilisé → même résultat que positif
    bmu_rint_result_t pos = rint_compute(26000.0f,  3.0f, 26045.0f, 26072.0f, 1000000LL);
    bmu_rint_result_t neg = rint_compute(26000.0f, -3.0f, 26045.0f, 26072.0f, 1000000LL);
    TEST_ASSERT_TRUE(pos.valid);
    TEST_ASSERT_TRUE(neg.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, pos.r_ohmic_mohm, neg.r_ohmic_mohm);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, pos.r_total_mohm, neg.r_total_mohm);
}

void test_compute_high_current_small_dv(void)
{
    // 10A, dV=5mV → R_ohmic = 5/10 = 0.5 mΩ — très faible mais valide
    bmu_rint_result_t res = rint_compute(26000.0f, 10.0f, 26005.0f, 26008.0f, 1000000LL);
    TEST_ASSERT_TRUE(res.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, res.r_ohmic_mohm);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.8f, res.r_total_mohm);
}

void test_compute_boundary_current(void)
{
    // 0.5A exact = seuil minimum → valide
    bmu_rint_result_t res = rint_compute(26000.0f, 0.5f, 26020.0f, 26030.0f, 1000000LL);
    TEST_ASSERT_TRUE(res.valid);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 40.0f, res.r_ohmic_mohm);  // 20mV/0.5A = 40 mΩ
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 60.0f, res.r_total_mohm);  // 30mV/0.5A = 60 mΩ
}

void test_compute_preserves_raw_values(void)
{
    // Les valeurs brutes doivent être copiées telles quelles dans le résultat
    bmu_rint_result_t res = rint_compute(26000.0f, 3.5f, 26052.5f, 26073.0f, 9999999LL);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 26000.0f, res.v_load_mv);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f,     res.i_load_a);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 26052.5f, res.v_ocv_fast_mv);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 26073.0f, res.v_ocv_stable_mv);
    TEST_ASSERT_EQUAL(9999999LL, res.timestamp_ms);
}

// == Tests : Formatage InfluxDB ==========================================

void test_influx_format_periodic(void)
{
    bmu_rint_result_t res = rint_compute(26000.0f, 3.0f, 26045.0f, 26072.0f, 1000000LL);
    TEST_ASSERT_TRUE(res.valid);

    char buf[256];
    rint_format_influx(buf, sizeof(buf), &res, 2, BMU_RINT_TRIGGER_PERIODIC, 1000000000000LL);

    // Vérifie présence des tags et champs essentiels
    TEST_ASSERT_NOT_NULL(strstr(buf, "rint,battery=2,trigger=periodic"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "r_ohmic_mohm="));
    TEST_ASSERT_NOT_NULL(strstr(buf, "r_total_mohm="));
    TEST_ASSERT_NOT_NULL(strstr(buf, "r_polar_mohm="));
    TEST_ASSERT_NOT_NULL(strstr(buf, "v_load_mv="));
    TEST_ASSERT_NOT_NULL(strstr(buf, "v_ocv_fast_mv="));
    TEST_ASSERT_NOT_NULL(strstr(buf, "v_ocv_stable_mv="));
    TEST_ASSERT_NOT_NULL(strstr(buf, "i_load_a="));
    // Timestamp en fin de ligne
    TEST_ASSERT_NOT_NULL(strstr(buf, "1000000000000"));
}

void test_influx_format_opportunistic(void)
{
    bmu_rint_result_t res = rint_compute(26000.0f, 3.0f, 26045.0f, 26072.0f, 2000000LL);
    TEST_ASSERT_TRUE(res.valid);

    char buf[256];
    rint_format_influx(buf, sizeof(buf), &res, 5, BMU_RINT_TRIGGER_OPPORTUNISTIC, 2000000000000LL);

    TEST_ASSERT_NOT_NULL(strstr(buf, "rint,battery=5,trigger=opportunistic"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "2000000000000"));
}

// == Tests : Formatage JSON ===============================================

void test_json_format_valid(void)
{
    bmu_rint_result_t res = rint_compute(26000.0f, 3.0f, 26045.0f, 26072.0f, 1234567890LL);
    TEST_ASSERT_TRUE(res.valid);

    char buf[512];
    rint_format_json(buf, sizeof(buf), &res, 7);

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"index\":7"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"valid\":true"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"timestamp_ms\":1234567890"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"r_ohmic_mohm\":"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"r_total_mohm\":"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"r_polar_mohm\":"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"v_load_mv\":"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"v_ocv_fast_mv\":"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"v_ocv_stable_mv\":"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"i_load_a\":"));
}

void test_json_format_invalid(void)
{
    // Mesure invalide (courant trop faible)
    bmu_rint_result_t res = rint_compute(26000.0f, 0.1f, 26045.0f, 26072.0f, 999LL);
    TEST_ASSERT_FALSE(res.valid);

    char buf[512];
    rint_format_json(buf, sizeof(buf), &res, 3);

    TEST_ASSERT_NOT_NULL(strstr(buf, "\"index\":3"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"valid\":false"));
}

// -- Unity setup/teardown -------------------------------------------------

void setUp(void)
{
    // Initialisation avant chaque test (si nécessaire)
}

void tearDown(void)
{
    // Cleanup après chaque test (si nécessaire)
}

// -- Entry point ----------------------------------------------------------

int main(void)
{
    UNITY_BEGIN();

    // Guards
    RUN_TEST(test_guard_normal_pass);
    RUN_TEST(test_guard_too_few_connected);
    RUN_TEST(test_guard_exactly_one_fail);
    RUN_TEST(test_guard_exactly_two_pass);
    RUN_TEST(test_guard_error_state_fail);
    RUN_TEST(test_guard_target_disconnected_fail);
    RUN_TEST(test_guard_measurement_active_fail);

    // Calcul
    RUN_TEST(test_compute_normal);
    RUN_TEST(test_compute_low_current_rejected);
    RUN_TEST(test_compute_negative_dv_rejected);
    RUN_TEST(test_compute_excessive_r_rejected);
    RUN_TEST(test_compute_r_total_less_than_r_ohmic_rejected);
    RUN_TEST(test_compute_negative_current_uses_abs);
    RUN_TEST(test_compute_high_current_small_dv);
    RUN_TEST(test_compute_boundary_current);
    RUN_TEST(test_compute_preserves_raw_values);

    // Formatage InfluxDB
    RUN_TEST(test_influx_format_periodic);
    RUN_TEST(test_influx_format_opportunistic);

    // Formatage JSON
    RUN_TEST(test_json_format_valid);
    RUN_TEST(test_json_format_invalid);

    return UNITY_END();
}
