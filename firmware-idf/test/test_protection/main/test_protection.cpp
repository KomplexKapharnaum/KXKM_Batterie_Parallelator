/**
 * @file test_protection.cpp
 * @brief Tests unitaires de protection BMU — port ESP-IDF (Unity)
 *
 * Port des 13 tests Arduino/PlatformIO vers ESP-IDF Unity.
 * Compile en mode host (g++) ou sur cible ESP32 via ESP-IDF.
 * Logique pure, pas de I2C ni hardware.
 *
 * Seuils alignes sur Kconfig defaults (sdkconfig.defaults) :
 *   BMU_MIN_VOLTAGE_MV   = 24000
 *   BMU_MAX_VOLTAGE_MV   = 30000
 *   BMU_MAX_CURRENT_MA   = 10000  (10 A)
 *   BMU_VOLTAGE_DIFF_MV  = 1000   (1 V)
 *   BMU_NB_SWITCH_MAX    = 5
 *   BMU_OVERCURRENT_FACTOR = 2000 (2.0x)
 */

#include <unity.h>

// -- Parametres de protection (Kconfig defaults, unites mV / mA) -------------
static const int BMU_MIN_VOLTAGE_MV    = 24000;
static const int BMU_MAX_VOLTAGE_MV    = 30000;
static const int BMU_MAX_CURRENT_MA    = 10000;  // 10 A
static const int BMU_VOLTAGE_DIFF_MV   = 1000;   // 1 V
static const int BMU_NB_SWITCH_MAX     = 5;
// BMU_OVERCURRENT_FACTOR = 2000 (2.0x) — reserve pour surintensité étendue

// -- Logique de protection (stub) --------------------------------------------

/**
 * Determine si une batterie doit etre deconnectee.
 *
 * @param voltage_mv      Tension batterie en mV
 * @param current_a       Courant batterie en A (positif = decharge)
 * @param voltage_max_mv  Tension max du parc en mV (pour desequilibre)
 */
static bool should_disconnect(float voltage_mv, float current_a, float voltage_max_mv) {
    // Sous-tension
    if (voltage_mv < BMU_MIN_VOLTAGE_MV) return true;
    // Sur-tension
    if (voltage_mv > BMU_MAX_VOLTAGE_MV) return true;
    // Sur-courant (decharge)
    float current_ma = current_a * 1000.0f;
    if (current_ma > BMU_MAX_CURRENT_MA)  return true;
    // Sur-courant (charge / negatif)
    if (current_ma < -BMU_MAX_CURRENT_MA) return true;
    // Desequilibre tension (comparaison en mV)
    if ((voltage_max_mv - voltage_mv) > BMU_VOLTAGE_DIFF_MV) return true;
    return false;
}

/**
 * Determine si la batterie est verrouillee definitivement.
 */
static bool should_permanent_lock(int nb_switch) {
    return nb_switch > BMU_NB_SWITCH_MAX;
}

// -- Tests -------------------------------------------------------------------

void test_undervoltage_disconnects(void) {
    TEST_ASSERT_TRUE(should_disconnect(23999, 0.0f, 27000));
    TEST_ASSERT_TRUE(should_disconnect(0, 0.0f, 27000));
}

void test_nominal_voltage_connects(void) {
    TEST_ASSERT_FALSE(should_disconnect(27000, 0.0f, 27000));
    TEST_ASSERT_FALSE(should_disconnect(24000, 0.0f, 24000));
}

void test_overvoltage_disconnects(void) {
    TEST_ASSERT_TRUE(should_disconnect(30001, 0.0f, 30001));
    TEST_ASSERT_TRUE(should_disconnect(35000, 0.0f, 35000));
}

void test_overcurrent_positive_disconnects(void) {
    TEST_ASSERT_TRUE(should_disconnect(27000, 10.1f, 27000));
    TEST_ASSERT_TRUE(should_disconnect(27000, 50.0f, 27000));
}

void test_overcurrent_negative_disconnects(void) {
    TEST_ASSERT_TRUE(should_disconnect(27000, -10.1f, 27000));
}

void test_overcurrent_large_negative_disconnects(void) {
    // Appel de courant de charge important -> deconnexion
    TEST_ASSERT_TRUE(should_disconnect(27000, -15.0f, 27000));
}

void test_nominal_current_connects(void) {
    TEST_ASSERT_FALSE(should_disconnect(27000, 5.0f, 27000));
    TEST_ASSERT_FALSE(should_disconnect(27000, -5.0f, 27000));
}

void test_voltage_imbalance_disconnects(void) {
    // batterie a 25 V, max du parc a 27 V -> diff = 2000 mV > seuil 1000 mV
    TEST_ASSERT_TRUE(should_disconnect(25000, 0.0f, 27000));
}

void test_voltage_imbalance_within_threshold_connects(void) {
    // batterie a 26.5 V, max a 27 V -> diff = 500 mV < seuil
    TEST_ASSERT_FALSE(should_disconnect(26500, 0.0f, 27000));
}

void test_voltage_imbalance_against_fleet_max(void) {
    // Parc 25-27V. Batterie a 25V, max=27V -> diff=2000 mV > 1000 mV -> deconnexion
    TEST_ASSERT_TRUE(should_disconnect(25000, 0.0f, 27000));
    // Batterie a 26.5V, max=27V -> diff=500 mV < 1000 mV -> connexion
    TEST_ASSERT_FALSE(should_disconnect(26500, 0.0f, 27000));
}

void test_permanent_lock_above_max(void) {
    TEST_ASSERT_TRUE(should_permanent_lock(BMU_NB_SWITCH_MAX + 1));
    TEST_ASSERT_TRUE(should_permanent_lock(100));
}

void test_no_permanent_lock_at_max(void) {
    TEST_ASSERT_FALSE(should_permanent_lock(BMU_NB_SWITCH_MAX));
    TEST_ASSERT_FALSE(should_permanent_lock(0));
}

void test_permanent_lock_at_max_plus_one(void) {
    // nb_switch = 6 (NB_SWITCH_MAX + 1) DOIT verrouiller definitivement
    TEST_ASSERT_TRUE(should_permanent_lock(6));
}

// -- Runner ------------------------------------------------------------------

#ifdef NATIVE_TEST
// Host build : main() classique
int main(void) {
#else
// ESP-IDF build : app_main appelé par le framework
extern "C" void app_main(void) {
#endif
    UNITY_BEGIN();
    RUN_TEST(test_undervoltage_disconnects);
    RUN_TEST(test_nominal_voltage_connects);
    RUN_TEST(test_overvoltage_disconnects);
    RUN_TEST(test_overcurrent_positive_disconnects);
    RUN_TEST(test_overcurrent_negative_disconnects);
    RUN_TEST(test_overcurrent_large_negative_disconnects);
    RUN_TEST(test_nominal_current_connects);
    RUN_TEST(test_voltage_imbalance_disconnects);
    RUN_TEST(test_voltage_imbalance_within_threshold_connects);
    RUN_TEST(test_voltage_imbalance_against_fleet_max);
    RUN_TEST(test_permanent_lock_above_max);
    RUN_TEST(test_no_permanent_lock_at_max);
    RUN_TEST(test_permanent_lock_at_max_plus_one);
#ifdef NATIVE_TEST
    return UNITY_END();
#else
    UNITY_END();
#endif
}
