#ifndef NATIVE_TEST
#define NATIVE_TEST
#endif
#include <unity.h>
#include <string.h>
#include "bmu_types.h"

#define BAL_HIGH_MV     200.0f
#define BAL_DUTY_ON     10
#define BAL_DUTY_OFF    5
#define BAL_MIN_CONN    2

typedef struct {
    int  on_counter;
    int  off_counter;
    bool balancing;
} bal_state_t;

static bool bal_should_disconnect(bal_state_t *bs, float v_mv, float v_mean,
                                   int nb_connected) {
    if (nb_connected < BAL_MIN_CONN) return false;
    if (bs->off_counter > 0) {
        bs->off_counter--;
        if (bs->off_counter == 0) {
            bs->balancing = false;
            bs->on_counter = BAL_DUTY_ON;
        }
        return false;
    }
    float delta = v_mv - v_mean;
    if (delta > BAL_HIGH_MV) {
        bs->on_counter--;
        if (bs->on_counter <= 0) {
            bs->balancing = true;
            bs->off_counter = BAL_DUTY_OFF;
            return true;
        }
    } else {
        bs->on_counter = BAL_DUTY_ON;
        bs->balancing = false;
    }
    return false;
}

void setUp(void) {}
void tearDown(void) {}

void test_bal_no_action_below_threshold(void) {
    bal_state_t bs = { .on_counter = BAL_DUTY_ON, .off_counter = 0, .balancing = false };
    bool disc = bal_should_disconnect(&bs, 27500.0f, 27400.0f, 4);
    TEST_ASSERT_FALSE(disc);
    TEST_ASSERT_FALSE(bs.balancing);
}

void test_bal_no_action_too_few_batteries(void) {
    bal_state_t bs = { .on_counter = BAL_DUTY_ON, .off_counter = 0, .balancing = false };
    bool disc = bal_should_disconnect(&bs, 28000.0f, 27000.0f, 1);
    TEST_ASSERT_FALSE(disc);
}

void test_bal_disconnect_after_on_counter_expires(void) {
    bal_state_t bs = { .on_counter = 1, .off_counter = 0, .balancing = false };
    bool disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
    TEST_ASSERT_TRUE(disc);
    TEST_ASSERT_TRUE(bs.balancing);
    TEST_ASSERT_EQUAL_INT(BAL_DUTY_OFF, bs.off_counter);
}

void test_bal_off_counter_counts_down(void) {
    bal_state_t bs = { .on_counter = 0, .off_counter = 3, .balancing = true };
    bool disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
    TEST_ASSERT_FALSE(disc);
    TEST_ASSERT_EQUAL_INT(2, bs.off_counter);
    TEST_ASSERT_TRUE(bs.balancing);
}

void test_bal_reconnect_after_off_expires(void) {
    bal_state_t bs = { .on_counter = 0, .off_counter = 1, .balancing = true };
    bool disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
    TEST_ASSERT_FALSE(disc);
    TEST_ASSERT_EQUAL_INT(0, bs.off_counter);
    TEST_ASSERT_FALSE(bs.balancing);
    TEST_ASSERT_EQUAL_INT(BAL_DUTY_ON, bs.on_counter);
}

void test_bal_reset_counter_when_voltage_drops(void) {
    bal_state_t bs = { .on_counter = 3, .off_counter = 0, .balancing = false };
    bool disc = bal_should_disconnect(&bs, 27250.0f, 27200.0f, 4);
    TEST_ASSERT_FALSE(disc);
    TEST_ASSERT_EQUAL_INT(BAL_DUTY_ON, bs.on_counter);
}

void test_bal_full_cycle(void) {
    bal_state_t bs = { .on_counter = BAL_DUTY_ON, .off_counter = 0, .balancing = false };
    for (int i = 0; i < BAL_DUTY_ON - 1; i++) {
        bool disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
        TEST_ASSERT_FALSE(disc);
    }
    bool disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
    TEST_ASSERT_TRUE(disc);
    TEST_ASSERT_TRUE(bs.balancing);
    for (int i = 0; i < BAL_DUTY_OFF; i++) {
        disc = bal_should_disconnect(&bs, 27500.0f, 27200.0f, 4);
        TEST_ASSERT_FALSE(disc);
    }
    TEST_ASSERT_FALSE(bs.balancing);
    TEST_ASSERT_EQUAL_INT(BAL_DUTY_ON, bs.on_counter);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bal_no_action_below_threshold);
    RUN_TEST(test_bal_no_action_too_few_batteries);
    RUN_TEST(test_bal_disconnect_after_on_counter_expires);
    RUN_TEST(test_bal_off_counter_counts_down);
    RUN_TEST(test_bal_reconnect_after_off_expires);
    RUN_TEST(test_bal_reset_counter_when_voltage_drops);
    RUN_TEST(test_bal_full_cycle);
    return UNITY_END();
}
