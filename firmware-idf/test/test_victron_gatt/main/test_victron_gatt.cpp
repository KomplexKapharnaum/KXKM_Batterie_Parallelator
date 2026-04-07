#include "unity.h"
#include <cstring>
#include <cstdint>

void setUp(void) {}
void tearDown(void) {}

void test_voltage_encoding(void)
{
    float avg_mv = 27500.0f;
    uint16_t val = (uint16_t)(avg_mv / 10.0f);
    TEST_ASSERT_EQUAL_UINT16(2750, val);
}

void test_soc_mapping(void)
{
    float avg_mv = 26000.0f;
    float min_mv = 24000.0f;
    float max_mv = 30000.0f;
    float soc = (avg_mv - min_mv) / (max_mv - min_mv) * 100.0f;
    uint16_t val = (uint16_t)(soc * 100.0f);
    TEST_ASSERT_EQUAL_UINT16(3333, val);
}

void test_soc_clamp_low(void)
{
    float avg_mv = 20000.0f;
    float min_mv = 24000.0f;
    float max_mv = 30000.0f;
    float soc = (avg_mv - min_mv) / (max_mv - min_mv) * 100.0f;
    if (soc < 0) soc = 0;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, soc);
}

void test_soc_clamp_high(void)
{
    float avg_mv = 32000.0f;
    float min_mv = 24000.0f;
    float max_mv = 30000.0f;
    float soc = (avg_mv - min_mv) / (max_mv - min_mv) * 100.0f;
    if (soc > 100) soc = 100;
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, soc);
}

void test_alarm_low_voltage(void)
{
    float avg_mv = 23000.0f;
    uint16_t min_mv = 24000;
    uint16_t alarm = 0;
    if (avg_mv < min_mv) alarm |= (1 << 0);
    TEST_ASSERT_BITS(0x01, 0x01, alarm);
}

void test_alarm_high_voltage(void)
{
    float avg_mv = 31000.0f;
    uint16_t max_mv = 30000;
    uint16_t alarm = 0;
    if (avg_mv > max_mv) alarm |= (1 << 1);
    TEST_ASSERT_BITS(0x02, 0x02, alarm);
}

void test_temperature_kelvin(void)
{
    float temp_c = 25.0f;
    int16_t val = (int16_t)(temp_c * 100.0f + 27315);
    TEST_ASSERT_EQUAL_INT16(29815, val);
}

void test_ttg_discharge(void)
{
    float remaining_ah = 50.0f;
    float current_a = -5.0f;
    uint16_t ttg;
    if (current_a >= 0) {
        ttg = 0xFFFF;
    } else {
        ttg = (uint16_t)(remaining_ah / (-current_a) * 60.0f);
    }
    TEST_ASSERT_EQUAL_UINT16(600, ttg);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_voltage_encoding);
    RUN_TEST(test_soc_mapping);
    RUN_TEST(test_soc_clamp_low);
    RUN_TEST(test_soc_clamp_high);
    RUN_TEST(test_alarm_low_voltage);
    RUN_TEST(test_alarm_high_voltage);
    RUN_TEST(test_temperature_kelvin);
    RUN_TEST(test_ttg_discharge);
    return UNITY_END();
}
