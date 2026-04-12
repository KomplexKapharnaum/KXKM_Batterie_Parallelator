#ifdef NATIVE_TEST

#include "unity.h"
#include <cstdint>

extern "C" void setUp(void) {}
extern "C" void tearDown(void) {}

void test_write_address_byte(void) {
    uint8_t addr = 0x40;
    uint8_t write_byte = (addr << 1) | 0;
    TEST_ASSERT_EQUAL_UINT8(0x80, write_byte);
}

void test_read_address_byte(void) {
    uint8_t addr = 0x40;
    uint8_t read_byte = (addr << 1) | 1;
    TEST_ASSERT_EQUAL_UINT8(0x81, read_byte);
}

void test_reg16_big_endian_pack(void) {
    uint16_t value = 0x1234;
    uint8_t buf[3];
    buf[0] = 0x05;
    buf[1] = (uint8_t)(value >> 8);
    buf[2] = (uint8_t)(value & 0xFF);
    TEST_ASSERT_EQUAL_UINT8(0x05, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x12, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x34, buf[2]);
}

void test_reg16_big_endian_unpack(void) {
    uint8_t buf[2] = { 0xAB, 0xCD };
    uint16_t value = ((uint16_t)buf[0] << 8) | buf[1];
    TEST_ASSERT_EQUAL_UINT16(0xABCD, value);
}

void test_half_period_50khz(void) {
    uint32_t freq = 50000;
    uint32_t half_period = 500000 / freq;
    TEST_ASSERT_EQUAL_UINT32(10, half_period);
}

void test_half_period_100khz(void) {
    uint32_t freq = 100000;
    uint32_t half_period = 500000 / freq;
    TEST_ASSERT_EQUAL_UINT32(5, half_period);
}

void test_address_range_ina237(void) {
    for (uint8_t a = 0x40; a <= 0x4F; a++) {
        uint8_t wb = (a << 1) | 0;
        TEST_ASSERT_TRUE(wb >= 0x80 && wb <= 0x9E);
    }
}

void test_address_range_tca9535(void) {
    for (uint8_t a = 0x20; a <= 0x27; a++) {
        uint8_t wb = (a << 1) | 0;
        TEST_ASSERT_TRUE(wb >= 0x40 && wb <= 0x4E);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_write_address_byte);
    RUN_TEST(test_read_address_byte);
    RUN_TEST(test_reg16_big_endian_pack);
    RUN_TEST(test_reg16_big_endian_unpack);
    RUN_TEST(test_half_period_50khz);
    RUN_TEST(test_half_period_100khz);
    RUN_TEST(test_address_range_ina237);
    RUN_TEST(test_address_range_tca9535);
    return UNITY_END();
}

#endif
