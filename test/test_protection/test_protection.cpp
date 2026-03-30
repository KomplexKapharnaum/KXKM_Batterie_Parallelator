#include <unity.h>

// ── Stub minimal pour les tests natifs ──────────────────────────────────────
#ifndef NATIVE_TEST
#error "Ce fichier doit être compilé avec -DNATIVE_TEST"
#endif

// Paramètres copiés depuis main.cpp
static const int ALERT_BAT_MIN_VOLTAGE = 24000; // mV
static const int ALERT_BAT_MAX_VOLTAGE = 30000; // mV
static const int ALERT_BAT_MAX_CURRENT = 1;     // A
static const int VOLTAGE_DIFF          = 1;     // V
static const int NB_SWITCH_MAX         = 5;

// Logique de protection extraite de BatterySwitchCtrl.h / main.cpp
static bool should_disconnect(float voltage_mv, float current_a, float voltage_max_v) {
    if (voltage_mv < ALERT_BAT_MIN_VOLTAGE) return true;
    if (voltage_mv > ALERT_BAT_MAX_VOLTAGE) return true;
    if (current_a > ALERT_BAT_MAX_CURRENT)  return true;
    if (current_a < -ALERT_BAT_MAX_CURRENT) return true;
    // déséquilibre tension
    float voltage_v = voltage_mv / 1000.0f;
    if ((voltage_max_v - voltage_v) > VOLTAGE_DIFF) return true;
    return false;
}

static bool should_permanent_lock(int nb_switch) {
    return nb_switch > NB_SWITCH_MAX;
}

// ── Tests ────────────────────────────────────────────────────────────────────

void test_undervoltage_disconnects(void) {
    TEST_ASSERT_TRUE(should_disconnect(23999, 0.0f, 27.0f));
    TEST_ASSERT_TRUE(should_disconnect(0, 0.0f, 27.0f));
}

void test_nominal_voltage_connects(void) {
    TEST_ASSERT_FALSE(should_disconnect(27000, 0.0f, 27.0f));
    TEST_ASSERT_FALSE(should_disconnect(24000, 0.0f, 24.0f));
}

void test_overvoltage_disconnects(void) {
    TEST_ASSERT_TRUE(should_disconnect(30001, 0.0f, 30.001f));
    TEST_ASSERT_TRUE(should_disconnect(35000, 0.0f, 35.0f));
}

void test_overcurrent_positive_disconnects(void) {
    TEST_ASSERT_TRUE(should_disconnect(27000, 1.1f, 27.0f));
    TEST_ASSERT_TRUE(should_disconnect(27000, 5.0f, 27.0f));
}

void test_overcurrent_negative_disconnects(void) {
    TEST_ASSERT_TRUE(should_disconnect(27000, -1.1f, 27.0f));
}

void test_nominal_current_connects(void) {
    TEST_ASSERT_FALSE(should_disconnect(27000, 0.5f, 27.0f));
    TEST_ASSERT_FALSE(should_disconnect(27000, -0.5f, 27.0f));
}

void test_voltage_imbalance_disconnects(void) {
    // batterie à 25 V, max du parc à 27 V → diff = 2 V > seuil 1 V
    TEST_ASSERT_TRUE(should_disconnect(25000, 0.0f, 27.0f));
}

void test_voltage_imbalance_within_threshold_connects(void) {
    // batterie à 26.5 V, max à 27 V → diff = 0.5 V < seuil
    TEST_ASSERT_FALSE(should_disconnect(26500, 0.0f, 27.0f));
}

void test_permanent_lock_above_max(void) {
    TEST_ASSERT_TRUE(should_permanent_lock(NB_SWITCH_MAX + 1));
    TEST_ASSERT_TRUE(should_permanent_lock(100));
}

void test_no_permanent_lock_at_max(void) {
    TEST_ASSERT_FALSE(should_permanent_lock(NB_SWITCH_MAX));
    TEST_ASSERT_FALSE(should_permanent_lock(0));
}

// ── Runner ───────────────────────────────────────────────────────────────────

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_undervoltage_disconnects);
    RUN_TEST(test_nominal_voltage_connects);
    RUN_TEST(test_overvoltage_disconnects);
    RUN_TEST(test_overcurrent_positive_disconnects);
    RUN_TEST(test_overcurrent_negative_disconnects);
    RUN_TEST(test_nominal_current_connects);
    RUN_TEST(test_voltage_imbalance_disconnects);
    RUN_TEST(test_voltage_imbalance_within_threshold_connects);
    RUN_TEST(test_permanent_lock_above_max);
    RUN_TEST(test_no_permanent_lock_at_max);
    return UNITY_END();
}
