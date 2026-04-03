/**
 * @file test_ble_victron.cpp
 * @brief Tests unitaires encodage Victron Instant Readout — host mode
 *
 * Valide les fonctions d'encoding des payloads BLE Victron:
 *   - Battery: remaining Ah, voltage, current, SoC, consumed Ah
 *   - Solar: charge state, error, yield, Ppv, Ibat, Vbat
 *   - MFR header: company ID (0x02E1), record type, counter
 *
 * Format: little-endian, scaling en unites decimalames
 *   Tension: x100 (mV -> 0.01V precision)
 *   Courant: x10  (A -> 0.1A precision, signed)
 *   Energie: x10  (Wh -> 10Wh chunks)
 *   SoC/Puissance: x10, x1
 *
 * Compile host: g++ -Wall -Wextra -DNATIVE_TEST -I../../components/unity/include \
 *   -o test_ble_victron main/test_ble_victron.cpp ../../components/unity/src/unity.c
 */

#include <unity.h>
#include <cstdint>
#include <cstring>

#define VICTRON_COMPANY_ID      0x02E1
#define VICTRON_RECORD_SOLAR    0x01
#define VICTRON_RECORD_BATTERY  0x02

/**
 * Encode batterie payload (10 bytes):
 *   [0:2]  remaining_ah (x10, uint16_t LE)
 *   [2:4]  voltage (x100, uint16_t LE)
 *   [4:6]  current (x10, int16_t LE, signed)
 *   [6:8]  soc (x10, uint16_t LE)
 *   [8:10] consumed_ah (x10, uint16_t LE)
 */
static int pack_battery_payload(uint8_t *plain, float avg_v, float total_i,
                                float soc, float ah_c, float ah_d)
{
    uint16_t remaining_ah = (uint16_t)(ah_c * 10.0f);
    uint16_t voltage = (uint16_t)(avg_v * 100.0f);
    int16_t current = (int16_t)(total_i * 10.0f);
    uint16_t soc_u16 = (uint16_t)(soc * 10.0f);
    uint16_t consumed_ah = (uint16_t)(ah_d * 10.0f);
    memcpy(plain + 0, &remaining_ah, 2);
    memcpy(plain + 2, &voltage, 2);
    memcpy(plain + 4, &current, 2);
    memcpy(plain + 6, &soc_u16, 2);
    memcpy(plain + 8, &consumed_ah, 2);
    return 10;
}

/**
 * Encode solar payload (10 bytes):
 *   [0]    charge state (uint8_t)
 *   [1]    error flags (uint8_t)
 *   [2:4]  yield (x10, uint16_t LE)
 *   [4:6]  ppv (uint16_t LE)
 *   [6:8]  ibat (x10, int16_t LE, signed)
 *   [8:10] vbat (x100, uint16_t LE)
 */
static int pack_solar_payload(uint8_t *plain, uint8_t cs, uint8_t err,
                              uint32_t yield_wh, uint16_t ppv,
                              float ibat, float vbat)
{
    plain[0] = cs;
    plain[1] = err;
    uint16_t yield = (uint16_t)(yield_wh / 10);
    int16_t ibat_i = (int16_t)(ibat * 10.0f);
    uint16_t vbat_u = (uint16_t)(vbat * 100.0f);
    memcpy(plain + 2, &yield, 2);
    memcpy(plain + 4, &ppv, 2);
    memcpy(plain + 6, &ibat_i, 2);
    memcpy(plain + 8, &vbat_u, 2);
    return 10;
}

/**
 * Encode MFR header (5 bytes):
 *   [0:2]  company_id (0x02E1, uint16_t LE)
 *   [2]    record_type (uint8_t)
 *   [3:5]  counter (uint16_t LE)
 */
static int build_mfr_header(uint8_t *buf, uint8_t record_type, uint16_t counter)
{
    buf[0] = (uint8_t)(VICTRON_COMPANY_ID & 0xFF);
    buf[1] = (uint8_t)(VICTRON_COMPANY_ID >> 8);
    buf[2] = record_type;
    buf[3] = (uint8_t)(counter & 0xFF);
    buf[4] = (uint8_t)(counter >> 8);
    return 5;
}

// ============================================================================
// Unity hooks
// ============================================================================

void setUp(void)
{
    // Initialization before each test (if needed)
}

void tearDown(void)
{
    // Cleanup after each test (if needed)
}

// ============================================================================
// Tests — Batterie payload
// ============================================================================

void test_battery_payload_size(void)
{
    uint8_t plain[16];
    int len = pack_battery_payload(plain, 26.4f, 4.1f, 50.0f, 12.0f, 8.0f);
    TEST_ASSERT_EQUAL_INT(10, len);
}

void test_battery_payload_remaining_ah(void)
{
    uint8_t plain[10];
    pack_battery_payload(plain, 0.0f, 0.0f, 0.0f, 12.5f, 0.0f);
    uint16_t remaining_ah;
    memcpy(&remaining_ah, plain + 0, 2);
    TEST_ASSERT_EQUAL_UINT16(125, remaining_ah);
}

void test_battery_payload_voltage_encoding(void)
{
    uint8_t plain[10];
    pack_battery_payload(plain, 26.85f, 0.0f, 0.0f, 0.0f, 0.0f);
    uint16_t voltage;
    memcpy(&voltage, plain + 2, 2);
    TEST_ASSERT_EQUAL_UINT16(2685, voltage);
}

void test_battery_payload_current_positive(void)
{
    uint8_t plain[10];
    pack_battery_payload(plain, 0.0f, 3.5f, 0.0f, 0.0f, 0.0f);
    int16_t current;
    memcpy(&current, plain + 4, 2);
    TEST_ASSERT_EQUAL_INT16(35, current);
}

void test_battery_payload_current_negative(void)
{
    uint8_t plain[10];
    pack_battery_payload(plain, 0.0f, -3.5f, 0.0f, 0.0f, 0.0f);
    int16_t current;
    memcpy(&current, plain + 4, 2);
    TEST_ASSERT_EQUAL_INT16(-35, current);
}

void test_battery_payload_soc(void)
{
    uint8_t plain[10];
    pack_battery_payload(plain, 0.0f, 0.0f, 75.5f, 0.0f, 0.0f);
    uint16_t soc;
    memcpy(&soc, plain + 6, 2);
    TEST_ASSERT_EQUAL_UINT16(755, soc);
}

void test_battery_payload_consumed_ah(void)
{
    uint8_t plain[10];
    pack_battery_payload(plain, 0.0f, 0.0f, 0.0f, 0.0f, 8.2f);
    uint16_t consumed_ah;
    memcpy(&consumed_ah, plain + 8, 2);
    TEST_ASSERT_EQUAL_UINT16(82, consumed_ah);
}

// ============================================================================
// Tests — Solar payload
// ============================================================================

void test_solar_payload_size(void)
{
    uint8_t plain[16];
    int len = pack_solar_payload(plain, 5, 0, 1250, 340, 12.3f, 27.1f);
    TEST_ASSERT_EQUAL_INT(10, len);
}

void test_solar_payload_charge_state(void)
{
    uint8_t plain[10];
    pack_solar_payload(plain, 5, 0, 0, 0, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT8(5, plain[0]);
}

void test_solar_payload_error_flags(void)
{
    uint8_t plain[10];
    pack_solar_payload(plain, 0, 0x03, 0, 0, 0.0f, 0.0f);
    TEST_ASSERT_EQUAL_UINT8(0x03, plain[1]);
}

void test_solar_payload_yield_encoding(void)
{
    uint8_t plain[10];
    pack_solar_payload(plain, 0, 0, 1250, 0, 0.0f, 0.0f);
    uint16_t yield;
    memcpy(&yield, plain + 2, 2);
    // 1250 Wh / 10 = 125
    TEST_ASSERT_EQUAL_UINT16(125, yield);
}

void test_solar_payload_ppv(void)
{
    uint8_t plain[10];
    pack_solar_payload(plain, 0, 0, 0, 340, 0.0f, 0.0f);
    uint16_t ppv;
    memcpy(&ppv, plain + 4, 2);
    TEST_ASSERT_EQUAL_UINT16(340, ppv);
}

void test_solar_payload_ibat_positive(void)
{
    uint8_t plain[10];
    pack_solar_payload(plain, 0, 0, 0, 0, 12.3f, 0.0f);
    int16_t ibat;
    memcpy(&ibat, plain + 6, 2);
    TEST_ASSERT_EQUAL_INT16(123, ibat);
}

void test_solar_payload_ibat_negative(void)
{
    uint8_t plain[10];
    pack_solar_payload(plain, 0, 0, 0, 0, -5.2f, 0.0f);
    int16_t ibat;
    memcpy(&ibat, plain + 6, 2);
    TEST_ASSERT_EQUAL_INT16(-52, ibat);
}

void test_solar_payload_vbat(void)
{
    uint8_t plain[10];
    pack_solar_payload(plain, 0, 0, 0, 0, 0.0f, 27.14f);
    uint16_t vbat;
    memcpy(&vbat, plain + 8, 2);
    TEST_ASSERT_EQUAL_UINT16(2714, vbat);
}

// ============================================================================
// Tests — MFR header
// ============================================================================

void test_mfr_header_size(void)
{
    uint8_t buf[8];
    int len = build_mfr_header(buf, VICTRON_RECORD_BATTERY, 0);
    TEST_ASSERT_EQUAL_INT(5, len);
}

void test_mfr_header_company_id_byte0(void)
{
    uint8_t buf[8];
    build_mfr_header(buf, VICTRON_RECORD_BATTERY, 42);
    TEST_ASSERT_EQUAL_UINT8(0xE1, buf[0]);
}

void test_mfr_header_company_id_byte1(void)
{
    uint8_t buf[8];
    build_mfr_header(buf, VICTRON_RECORD_BATTERY, 42);
    TEST_ASSERT_EQUAL_UINT8(0x02, buf[1]);
}

void test_mfr_header_record_type_solar(void)
{
    uint8_t buf[8];
    build_mfr_header(buf, VICTRON_RECORD_SOLAR, 0);
    TEST_ASSERT_EQUAL_UINT8(VICTRON_RECORD_SOLAR, buf[2]);
}

void test_mfr_header_record_type_battery(void)
{
    uint8_t buf[8];
    build_mfr_header(buf, VICTRON_RECORD_BATTERY, 0);
    TEST_ASSERT_EQUAL_UINT8(VICTRON_RECORD_BATTERY, buf[2]);
}

void test_mfr_header_counter_small(void)
{
    uint8_t buf[8];
    build_mfr_header(buf, 0, 42);
    TEST_ASSERT_EQUAL_UINT8(0x2A, buf[3]);  // 42 = 0x2A
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[4]);
}

void test_mfr_header_counter_large(void)
{
    uint8_t buf[8];
    build_mfr_header(buf, 0, 0x1234);
    TEST_ASSERT_EQUAL_UINT8(0x34, buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0x12, buf[4]);
}

void test_mfr_header_counter_max(void)
{
    uint8_t buf[8];
    build_mfr_header(buf, 0, 0xFFFF);
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[3]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[4]);
}

// ============================================================================
// Integration tests
// ============================================================================

void test_battery_payload_realistic_values(void)
{
    uint8_t plain[10];
    // Realistic battery: 26.4V, 4.1A discharge, 50% SoC, 12Ah remaining, 8Ah consumed
    int len = pack_battery_payload(plain, 26.4f, 4.1f, 50.0f, 12.0f, 8.0f);
    TEST_ASSERT_EQUAL_INT(10, len);

    uint16_t voltage;
    int16_t current;
    uint16_t soc;
    uint16_t remaining_ah;
    uint16_t consumed_ah;

    memcpy(&remaining_ah, plain + 0, 2);
    memcpy(&voltage, plain + 2, 2);
    memcpy(&current, plain + 4, 2);
    memcpy(&soc, plain + 6, 2);
    memcpy(&consumed_ah, plain + 8, 2);

    TEST_ASSERT_EQUAL_UINT16(120, remaining_ah);   // 12 * 10
    TEST_ASSERT_EQUAL_UINT16(2640, voltage);       // 26.4 * 100
    TEST_ASSERT_EQUAL_INT16(41, current);          // 4.1 * 10
    TEST_ASSERT_EQUAL_UINT16(500, soc);            // 50.0 * 10
    TEST_ASSERT_EQUAL_UINT16(80, consumed_ah);     // 8 * 10
}

void test_solar_payload_realistic_values(void)
{
    uint8_t plain[10];
    // Realistic solar: bulk charge, no errors, 1.25kWh yield, 340W, 12.3A charge, 27.1V
    int len = pack_solar_payload(plain, 5, 0, 1250, 340, 12.3f, 27.1f);
    TEST_ASSERT_EQUAL_INT(10, len);

    uint16_t yield, ppv, vbat;
    int16_t ibat;

    memcpy(&yield, plain + 2, 2);
    memcpy(&ppv, plain + 4, 2);
    memcpy(&ibat, plain + 6, 2);
    memcpy(&vbat, plain + 8, 2);

    TEST_ASSERT_EQUAL_UINT16(125, yield);          // 1250 / 10
    TEST_ASSERT_EQUAL_UINT16(340, ppv);
    TEST_ASSERT_EQUAL_INT16(123, ibat);            // 12.3 * 10
    TEST_ASSERT_EQUAL_UINT16(2710, vbat);          // 27.1 * 100
}

int main(void)
{
    UNITY_BEGIN();

    // Battery payload
    RUN_TEST(test_battery_payload_size);
    RUN_TEST(test_battery_payload_remaining_ah);
    RUN_TEST(test_battery_payload_voltage_encoding);
    RUN_TEST(test_battery_payload_current_positive);
    RUN_TEST(test_battery_payload_current_negative);
    RUN_TEST(test_battery_payload_soc);
    RUN_TEST(test_battery_payload_consumed_ah);

    // Solar payload
    RUN_TEST(test_solar_payload_size);
    RUN_TEST(test_solar_payload_charge_state);
    RUN_TEST(test_solar_payload_error_flags);
    RUN_TEST(test_solar_payload_yield_encoding);
    RUN_TEST(test_solar_payload_ppv);
    RUN_TEST(test_solar_payload_ibat_positive);
    RUN_TEST(test_solar_payload_ibat_negative);
    RUN_TEST(test_solar_payload_vbat);

    // MFR header
    RUN_TEST(test_mfr_header_size);
    RUN_TEST(test_mfr_header_company_id_byte0);
    RUN_TEST(test_mfr_header_company_id_byte1);
    RUN_TEST(test_mfr_header_record_type_solar);
    RUN_TEST(test_mfr_header_record_type_battery);
    RUN_TEST(test_mfr_header_counter_small);
    RUN_TEST(test_mfr_header_counter_large);
    RUN_TEST(test_mfr_header_counter_max);

    // Integration
    RUN_TEST(test_battery_payload_realistic_values);
    RUN_TEST(test_solar_payload_realistic_values);

    return UNITY_END();
}
