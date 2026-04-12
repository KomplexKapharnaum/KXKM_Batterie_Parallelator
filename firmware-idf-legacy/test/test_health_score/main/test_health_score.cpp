#ifndef NATIVE_TEST
#define NATIVE_TEST
#endif
#include <unity.h>
#include "bmu_types.h"

// Health score helper (pure logic, tested independently)
static void health_record_success(bmu_device_health_t *h) {
    if (h->score <= BMU_HEALTH_SCORE_MAX - BMU_HEALTH_OK_INCR)
        h->score += BMU_HEALTH_OK_INCR;
    else
        h->score = BMU_HEALTH_SCORE_MAX;
    h->consec_fails = 0;
}

static void health_record_failure(bmu_device_health_t *h) {
    if (h->score >= BMU_HEALTH_FAIL_DECR)
        h->score -= BMU_HEALTH_FAIL_DECR;
    else
        h->score = 0;
    h->consec_fails++;
}

void setUp(void) {}
void tearDown(void) {}

void test_health_init_at_max(void) {
    bmu_device_health_t h = { .score = BMU_HEALTH_SCORE_INIT, .consec_fails = 0 };
    TEST_ASSERT_EQUAL_UINT8(100, h.score);
    TEST_ASSERT_EQUAL_UINT8(0, h.consec_fails);
}

void test_health_success_increments(void) {
    bmu_device_health_t h = { .score = 50, .consec_fails = 3 };
    health_record_success(&h);
    TEST_ASSERT_EQUAL_UINT8(55, h.score);
    TEST_ASSERT_EQUAL_UINT8(0, h.consec_fails);
}

void test_health_success_caps_at_100(void) {
    bmu_device_health_t h = { .score = 98, .consec_fails = 0 };
    health_record_success(&h);
    TEST_ASSERT_EQUAL_UINT8(100, h.score);
}

void test_health_failure_decrements(void) {
    bmu_device_health_t h = { .score = 100, .consec_fails = 0 };
    health_record_failure(&h);
    TEST_ASSERT_EQUAL_UINT8(80, h.score);
    TEST_ASSERT_EQUAL_UINT8(1, h.consec_fails);
}

void test_health_failure_floors_at_zero(void) {
    bmu_device_health_t h = { .score = 10, .consec_fails = 0 };
    health_record_failure(&h);
    TEST_ASSERT_EQUAL_UINT8(0, h.score);
}

void test_health_five_failures_reaches_zero(void) {
    bmu_device_health_t h = { .score = 100, .consec_fails = 0 };
    for (int i = 0; i < 5; i++) health_record_failure(&h);
    TEST_ASSERT_EQUAL_UINT8(0, h.score);
    TEST_ASSERT_EQUAL_UINT8(5, h.consec_fails);
}

void test_health_twenty_successes_full_recovery(void) {
    bmu_device_health_t h = { .score = 0, .consec_fails = 5 };
    for (int i = 0; i < 20; i++) health_record_success(&h);
    TEST_ASSERT_EQUAL_UINT8(100, h.score);
}

void test_health_warn_threshold(void) {
    bmu_device_health_t h = { .score = 60, .consec_fails = 0 };
    TEST_ASSERT_TRUE(h.score >= BMU_HEALTH_THRESH_WARN);
    health_record_failure(&h);
    TEST_ASSERT_TRUE(h.score < BMU_HEALTH_THRESH_WARN);
}

void test_health_critical_threshold(void) {
    bmu_device_health_t h = { .score = 40, .consec_fails = 0 };
    health_record_failure(&h);  // 40 → 20
    TEST_ASSERT_TRUE(h.score < BMU_HEALTH_THRESH_CRIT);
}

void test_health_reconnect_hysteresis(void) {
    bmu_device_health_t h = { .score = 25, .consec_fails = 0 };
    TEST_ASSERT_TRUE(h.score < BMU_HEALTH_THRESH_CRIT);
    for (int i = 0; i < 7; i++) health_record_success(&h);
    TEST_ASSERT_TRUE(h.score >= BMU_HEALTH_THRESH_RECONNECT);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_health_init_at_max);
    RUN_TEST(test_health_success_increments);
    RUN_TEST(test_health_success_caps_at_100);
    RUN_TEST(test_health_failure_decrements);
    RUN_TEST(test_health_failure_floors_at_zero);
    RUN_TEST(test_health_five_failures_reaches_zero);
    RUN_TEST(test_health_twenty_successes_full_recovery);
    RUN_TEST(test_health_warn_threshold);
    RUN_TEST(test_health_critical_threshold);
    RUN_TEST(test_health_reconnect_hysteresis);
    return UNITY_END();
}
