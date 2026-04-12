/**
 * @file test_ble_soh.cpp
 * @brief Tests unitaires BLE SOH characteristic pack/unpack — host (Unity)
 *
 * Verifie le format binaire de la struct ble_soh_char_t (7 octets, little-endian).
 * Pas de hardware necessaire.
 */

#include <unity.h>
#include <cstdint>
#include <cstring>
#include <cmath>

/* ── Struct miroir de bmu_ble_battery_svc.cpp ────────────────────────── */

typedef struct __attribute__((packed)) {
    uint8_t  soh_pct;
    uint16_t r_ohmic_mohm_x10;
    uint16_t r_total_mohm_x10;
    uint8_t  rint_valid;
    uint8_t  soh_confidence;
} ble_soh_char_t;

static_assert(sizeof(ble_soh_char_t) == 7, "ble_soh_char_t must be 7 bytes");

/* ── Helper: pack payload manually ───────────────────────────────────── */

static void build_test_payload(ble_soh_char_t *out,
                                float soh_0_1,
                                float r_ohmic_mohm, float r_total_mohm,
                                bool valid, uint8_t confidence)
{
    out->soh_pct           = (soh_0_1 >= 0.0f) ? (uint8_t)(soh_0_1 * 100.0f) : 0;
    out->r_ohmic_mohm_x10  = (uint16_t)(r_ohmic_mohm * 10.0f);
    out->r_total_mohm_x10  = (uint16_t)(r_total_mohm * 10.0f);
    out->rint_valid        = valid ? 1 : 0;
    out->soh_confidence    = confidence;
}

/* ── Helper: unpack from raw bytes (simulates app parser) ────────────── */

typedef struct {
    int   soh_pct;
    float r_ohmic_mohm;
    float r_total_mohm;
    bool  rint_valid;
    int   soh_confidence;
} parsed_soh_t;

static parsed_soh_t unpack_soh(const uint8_t *buf)
{
    parsed_soh_t p;
    p.soh_pct       = buf[0];
    p.r_ohmic_mohm  = (float)((uint16_t)(buf[1] | (buf[2] << 8))) / 10.0f;
    p.r_total_mohm  = (float)((uint16_t)(buf[3] | (buf[4] << 8))) / 10.0f;
    p.rint_valid    = buf[5] != 0;
    p.soh_confidence = buf[6];
    return p;
}

/* ── Tests ───────────────────────────────────────────────────────────── */

void test_soh_pack_healthy_battery(void)
{
    ble_soh_char_t payload;
    build_test_payload(&payload, 0.92f, 15.2f, 18.5f, true, 100);

    const uint8_t *raw = (const uint8_t *)&payload;
    parsed_soh_t p = unpack_soh(raw);

    TEST_ASSERT_EQUAL_INT(92, p.soh_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 15.2f, p.r_ohmic_mohm);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 18.5f, p.r_total_mohm);
    TEST_ASSERT_TRUE(p.rint_valid);
    TEST_ASSERT_EQUAL_INT(100, p.soh_confidence);
}

void test_soh_pack_invalid_rint(void)
{
    ble_soh_char_t payload;
    build_test_payload(&payload, 0.85f, 0.0f, 0.0f, false, 50);

    const uint8_t *raw = (const uint8_t *)&payload;
    parsed_soh_t p = unpack_soh(raw);

    TEST_ASSERT_EQUAL_INT(85, p.soh_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, p.r_ohmic_mohm);
    TEST_ASSERT_FALSE(p.rint_valid);
    TEST_ASSERT_EQUAL_INT(50, p.soh_confidence);
}

void test_soh_pack_not_yet_computed(void)
{
    ble_soh_char_t payload;
    build_test_payload(&payload, -1.0f, 0.0f, 0.0f, false, 0);

    const uint8_t *raw = (const uint8_t *)&payload;
    parsed_soh_t p = unpack_soh(raw);

    TEST_ASSERT_EQUAL_INT(0, p.soh_pct);
    TEST_ASSERT_FALSE(p.rint_valid);
    TEST_ASSERT_EQUAL_INT(0, p.soh_confidence);
}

void test_soh_pack_boundary_100_percent(void)
{
    ble_soh_char_t payload;
    build_test_payload(&payload, 1.0f, 5.0f, 6.0f, true, 100);

    const uint8_t *raw = (const uint8_t *)&payload;
    parsed_soh_t p = unpack_soh(raw);

    TEST_ASSERT_EQUAL_INT(100, p.soh_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 5.0f, p.r_ohmic_mohm);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 6.0f, p.r_total_mohm);
}

void test_soh_pack_high_resistance(void)
{
    /* R_ohmic = 200 mOhm -> x10 = 2000 (fits in uint16) */
    ble_soh_char_t payload;
    build_test_payload(&payload, 0.45f, 200.0f, 350.0f, true, 80);

    const uint8_t *raw = (const uint8_t *)&payload;
    parsed_soh_t p = unpack_soh(raw);

    TEST_ASSERT_EQUAL_INT(45, p.soh_pct);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 200.0f, p.r_ohmic_mohm);
    TEST_ASSERT_FLOAT_WITHIN(0.15f, 350.0f, p.r_total_mohm);
    TEST_ASSERT_TRUE(p.rint_valid);
}

void test_soh_struct_size(void)
{
    TEST_ASSERT_EQUAL_INT(7, sizeof(ble_soh_char_t));
}

void test_soh_multi_battery_concat(void)
{
    /* Simulate 4 batteries concatenated (as sent in BLE characteristic) */
    uint8_t buf[7 * 4];
    for (int i = 0; i < 4; i++) {
        ble_soh_char_t payload;
        float soh = 0.90f - (i * 0.05f);
        float r_ohm = 10.0f + i * 5.0f;
        float r_tot = 15.0f + i * 7.0f;
        build_test_payload(&payload, soh, r_ohm, r_tot, true, 100 - i * 10);
        memcpy(&buf[i * 7], &payload, 7);
    }

    /* Parse each battery from the concatenated buffer */
    for (int i = 0; i < 4; i++) {
        parsed_soh_t p = unpack_soh(&buf[i * 7]);
        int expected_soh = (int)((0.90f - i * 0.05f) * 100.0f);
        TEST_ASSERT_INT_WITHIN(1, expected_soh, p.soh_pct);
        TEST_ASSERT_TRUE(p.rint_valid);
        TEST_ASSERT_EQUAL_INT(100 - i * 10, p.soh_confidence);
    }
}

extern "C" void app_main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_soh_struct_size);
    RUN_TEST(test_soh_pack_healthy_battery);
    RUN_TEST(test_soh_pack_invalid_rint);
    RUN_TEST(test_soh_pack_not_yet_computed);
    RUN_TEST(test_soh_pack_boundary_100_percent);
    RUN_TEST(test_soh_pack_high_resistance);
    RUN_TEST(test_soh_multi_battery_concat);
    UNITY_END();
}
