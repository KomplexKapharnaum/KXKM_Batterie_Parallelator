#ifdef NATIVE_TEST

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

#include "unity.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

typedef struct {
    float    battery_voltage_v;
    float    battery_current_a;
    float    panel_voltage_v;
    uint16_t panel_power_w;
    uint8_t  charge_state;
    uint8_t  error_code;
    uint32_t yield_today_wh;
    bool     valid;
} vedirect_data_t;

static vedirect_data_t s_data;

static void parse_field(const char *label, const char *value)
{
    if (strcmp(label, "V") == 0) s_data.battery_voltage_v = atof(value) / 1000.0f;
    else if (strcmp(label, "I") == 0) s_data.battery_current_a = atof(value) / 1000.0f;
    else if (strcmp(label, "VPV") == 0) s_data.panel_voltage_v = atof(value) / 1000.0f;
    else if (strcmp(label, "PPV") == 0) s_data.panel_power_w = (uint16_t)atoi(value);
    else if (strcmp(label, "CS") == 0) s_data.charge_state = (uint8_t)atoi(value);
    else if (strcmp(label, "ERR") == 0) s_data.error_code = (uint8_t)atoi(value);
    else if (strcmp(label, "H20") == 0) s_data.yield_today_wh = (uint32_t)(atoi(value) * 10);
}

static bool validate_checksum(const uint8_t *frame, int len)
{
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) sum += frame[i];
    return (sum == 0);
}

static bool parse_frame(const char *frame)
{
    memset(&s_data, 0, sizeof(s_data));
    const char *p = frame;
    while (*p) {
        const char *tab = strchr(p, '\t');
        if (!tab) break;
        char label[16] = {};
        int llen = (int)(tab - p);
        if (llen > 15) llen = 15;
        memcpy(label, p, llen);
        const char *eol = strchr(tab + 1, '\r');
        if (!eol) eol = strchr(tab + 1, '\n');
        if (!eol) eol = tab + 1 + strlen(tab + 1);
        char value[32] = {};
        int vlen = (int)(eol - tab - 1);
        if (vlen > 31) vlen = 31;
        memcpy(value, tab + 1, vlen);
        if (strcmp(label, "Checksum") != 0) parse_field(label, value);
        p = eol;
        while (*p == '\r' || *p == '\n') p++;
    }
    s_data.valid = true;
    return true;
}

void test_parse_voltage(void) {
    parse_frame("V\t27140\r\n");
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 27.14f, s_data.battery_voltage_v);
}
void test_parse_current_positive(void) {
    parse_frame("I\t12300\r\n");
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.3f, s_data.battery_current_a);
}
void test_parse_current_negative(void) {
    parse_frame("I\t-500\r\n");
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -0.5f, s_data.battery_current_a);
}
void test_parse_panel_voltage(void) {
    parse_frame("VPV\t45200\r\n");
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 45.2f, s_data.panel_voltage_v);
}
void test_parse_panel_power(void) {
    parse_frame("PPV\t340\r\n");
    TEST_ASSERT_EQUAL_UINT16(340, s_data.panel_power_w);
}
void test_parse_charge_state(void) {
    parse_frame("CS\t5\r\n");
    TEST_ASSERT_EQUAL_UINT8(5, s_data.charge_state);
}
void test_parse_yield_today(void) {
    parse_frame("H20\t125\r\n");
    TEST_ASSERT_EQUAL_UINT32(1250, s_data.yield_today_wh);
}
void test_parse_multi_field(void) {
    parse_frame("V\t27140\r\nI\t12300\r\nVPV\t45200\r\nPPV\t340\r\nCS\t5\r\nERR\t0\r\nH20\t125\r\n");
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 27.14f, s_data.battery_voltage_v);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 12.3f, s_data.battery_current_a);
    TEST_ASSERT_EQUAL_UINT16(340, s_data.panel_power_w);
    TEST_ASSERT_EQUAL_UINT8(5, s_data.charge_state);
}
void test_checksum_valid(void) {
    uint8_t frame[] = {0x41, 0x09, 0x31, 0x0D, 0x0A};
    uint8_t sum = 0;
    for (int i = 0; i < 5; i++) sum += frame[i];
    uint8_t full[6];
    memcpy(full, frame, 5);
    full[5] = (uint8_t)(0 - sum);
    TEST_ASSERT_TRUE(validate_checksum(full, 6));
}
void test_checksum_invalid(void) {
    uint8_t frame[] = {0x41, 0x09, 0x31, 0x0D, 0x0A, 0x00};
    TEST_ASSERT_FALSE(validate_checksum(frame, 6));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_voltage);
    RUN_TEST(test_parse_current_positive);
    RUN_TEST(test_parse_current_negative);
    RUN_TEST(test_parse_panel_voltage);
    RUN_TEST(test_parse_panel_power);
    RUN_TEST(test_parse_charge_state);
    RUN_TEST(test_parse_yield_today);
    RUN_TEST(test_parse_multi_field);
    RUN_TEST(test_checksum_valid);
    RUN_TEST(test_checksum_invalid);
    return UNITY_END();
}

#endif
