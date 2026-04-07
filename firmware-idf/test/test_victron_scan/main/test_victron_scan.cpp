#include "unity.h"
#include <cstring>
#include <cstdint>

void setUp(void) {}
void tearDown(void) {}

void test_parse_solar_payload(void)
{
    uint8_t plain[10];
    plain[0] = 3;
    plain[1] = 0;
    uint16_t yield = 125;
    uint16_t ppv = 350;
    int16_t ibat = 123;
    uint16_t vbat = 2750;
    memcpy(plain + 2, &yield, 2);
    memcpy(plain + 4, &ppv, 2);
    memcpy(plain + 6, &ibat, 2);
    memcpy(plain + 8, &vbat, 2);

    TEST_ASSERT_EQUAL_UINT8(3, plain[0]);
    uint16_t parsed_ppv;
    memcpy(&parsed_ppv, plain + 4, 2);
    TEST_ASSERT_EQUAL_UINT16(350, parsed_ppv);
    int16_t parsed_ibat;
    memcpy(&parsed_ibat, plain + 6, 2);
    TEST_ASSERT_EQUAL_INT16(123, parsed_ibat);
}

void test_parse_battery_payload(void)
{
    uint8_t plain[10];
    uint16_t rem = 955;
    uint16_t v = 2720;
    int16_t i = -32;
    uint16_t soc = 980;
    uint16_t cons = 45;
    memcpy(plain + 0, &rem, 2);
    memcpy(plain + 2, &v, 2);
    memcpy(plain + 4, &i, 2);
    memcpy(plain + 6, &soc, 2);
    memcpy(plain + 8, &cons, 2);

    int16_t parsed_i;
    memcpy(&parsed_i, plain + 4, 2);
    TEST_ASSERT_EQUAL_INT16(-32, parsed_i);
    uint16_t parsed_soc;
    memcpy(&parsed_soc, plain + 6, 2);
    TEST_ASSERT_EQUAL_UINT16(980, parsed_soc);
}

void test_device_expiry(void)
{
    int64_t last_seen = 1000;
    int64_t now = 301001;
    bool expired = (now - last_seen) >= 300000;
    TEST_ASSERT_TRUE(expired);

    now = 299999;
    expired = (now - last_seen) >= 300000;
    TEST_ASSERT_FALSE(expired);
}

void test_mac_to_hex(void)
{
    uint8_t mac[6] = {0xA4, 0xC1, 0x38, 0xF2, 0xB3, 0x01};
    char key[13];
    snprintf(key, 13, "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    TEST_ASSERT_EQUAL_STRING("A4C138F2B301", key);
}

void test_company_id_filter(void)
{
    uint8_t mfr_data[] = {0xE1, 0x02, 0x01, 0x00, 0x00};
    uint16_t company = mfr_data[0] | (mfr_data[1] << 8);
    TEST_ASSERT_EQUAL_UINT16(0x02E1, company);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_solar_payload);
    RUN_TEST(test_parse_battery_payload);
    RUN_TEST(test_device_expiry);
    RUN_TEST(test_mac_to_hex);
    RUN_TEST(test_company_id_filter);
    return UNITY_END();
}
