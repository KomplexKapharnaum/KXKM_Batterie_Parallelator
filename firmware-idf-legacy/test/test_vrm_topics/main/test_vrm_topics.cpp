/**
 * @file test_vrm_topics.cpp
 * @brief Tests unitaires format VRM — portée ESP-IDF (Unity)
 *
 * Tests de la formatage MQTT des topics et payloads VRM pour intégration Victron.
 * Logique pure, pas de MQTT réel ni hardware.
 *
 * Couverture :
 *   - Formatage des topics VRM (N/<portal_id>/<path>, R/<portal_id>/keepalive)
 *   - Sérialisation payloads JSON (float, int, string)
 *   - Estimation SOC par tension moyenne (linear, clamped)
 */

#include <unity.h>
#include <cstdio>
#include <cstring>
#include <cmath>

// -- Helpers formatage VRM ------------------------------------------------

/**
 * Formate un topic VRM standard.
 * Format : N/<portal_id>/<path>
 *
 * @param buf     Buffer de sortie
 * @param len     Taille du buffer
 * @param portal_id  Portal Victron ID (ex: "70b3d549969af37b")
 * @param path    Chemin du topic (ex: "solarcharger/0/Pv/V")
 */
static void vrm_format_topic(char *buf, size_t len, const char *portal_id, const char *path)
{
    snprintf(buf, len, "N/%s/%s", portal_id, path);
}

/**
 * Formate un payload JSON float pour VRM.
 * Format : {"value":<float>}
 *
 * @param buf   Buffer de sortie
 * @param len   Taille du buffer
 * @param val   Valeur float à encoder
 */
static void vrm_format_float(char *buf, size_t len, float val)
{
    snprintf(buf, len, "{\"value\":%.2f}", val);
}

/**
 * Formate un payload JSON entier pour VRM.
 * Format : {"value":<int>}
 *
 * @param buf   Buffer de sortie
 * @param len   Taille du buffer
 * @param val   Valeur entière à encoder
 */
static void vrm_format_int(char *buf, size_t len, int val)
{
    snprintf(buf, len, "{\"value\":%d}", val);
}

/**
 * Formate un payload JSON string pour VRM.
 * Format : {"value":"<string>"}
 *
 * @param buf   Buffer de sortie
 * @param len   Taille du buffer
 * @param val   String à encoder
 */
static void vrm_format_str(char *buf, size_t len, const char *val)
{
    snprintf(buf, len, "{\"value\":\"%s\"}", val);
}

/**
 * Estime SOC (State of Charge) par tension moyenne.
 * Lineaire : SOC = (V_avg - V_min) / (V_max - V_min) * 100
 * Clamped entre [0, 100]
 *
 * @param avg_v  Tension moyenne batterie (en V)
 * @param v_min  Tension minimale (empty) (en V)
 * @param v_max  Tension maximale (full) (en V)
 * @return SOC en % [0, 100]
 */
static float estimate_soc(float avg_v, float v_min, float v_max)
{
    float soc = (avg_v - v_min) / (v_max - v_min) * 100.0f;
    if (soc < 0) soc = 0;
    if (soc > 100) soc = 100;
    return soc;
}

// -- Tests : Topics VRM --------------------------------------------------

void test_vrm_topic_solar_voltage(void)
{
    char topic[128];
    vrm_format_topic(topic, sizeof(topic), "70b3d549969af37b", "solarcharger/0/Pv/V");
    TEST_ASSERT_EQUAL_STRING("N/70b3d549969af37b/solarcharger/0/Pv/V", topic);
}

void test_vrm_topic_battery_soc(void)
{
    char topic[128];
    vrm_format_topic(topic, sizeof(topic), "70b3d549969af37b", "battery/0/Soc");
    TEST_ASSERT_EQUAL_STRING("N/70b3d549969af37b/battery/0/Soc", topic);
}

void test_vrm_topic_keepalive(void)
{
    char topic[64];
    snprintf(topic, sizeof(topic), "R/%s/keepalive", "70b3d549969af37b");
    TEST_ASSERT_EQUAL_STRING("R/70b3d549969af37b/keepalive", topic);
}

void test_vrm_topic_battery_voltage(void)
{
    char topic[128];
    vrm_format_topic(topic, sizeof(topic), "abcd1234efgh5678", "battery/0/V");
    TEST_ASSERT_EQUAL_STRING("N/abcd1234efgh5678/battery/0/V", topic);
}

void test_vrm_topic_battery_current(void)
{
    char topic[128];
    vrm_format_topic(topic, sizeof(topic), "xyz9999aaaa8888", "battery/0/I");
    TEST_ASSERT_EQUAL_STRING("N/xyz9999aaaa8888/battery/0/I", topic);
}

// -- Tests : Payloads JSON -----------------------------------------------

void test_vrm_payload_float(void)
{
    char json[32];
    vrm_format_float(json, sizeof(json), 26.85f);
    TEST_ASSERT_EQUAL_STRING("{\"value\":26.85}", json);
}

void test_vrm_payload_float_zero(void)
{
    char json[32];
    vrm_format_float(json, sizeof(json), 0.0f);
    TEST_ASSERT_EQUAL_STRING("{\"value\":0.00}", json);
}

void test_vrm_payload_float_negative(void)
{
    char json[32];
    vrm_format_float(json, sizeof(json), -15.42f);
    TEST_ASSERT_EQUAL_STRING("{\"value\":-15.42}", json);
}

void test_vrm_payload_int(void)
{
    char json[32];
    vrm_format_int(json, sizeof(json), 340);
    TEST_ASSERT_EQUAL_STRING("{\"value\":340}", json);
}

void test_vrm_payload_int_zero(void)
{
    char json[32];
    vrm_format_int(json, sizeof(json), 0);
    TEST_ASSERT_EQUAL_STRING("{\"value\":0}", json);
}

void test_vrm_payload_int_negative(void)
{
    char json[32];
    vrm_format_int(json, sizeof(json), -250);
    TEST_ASSERT_EQUAL_STRING("{\"value\":-250}", json);
}

void test_vrm_payload_str(void)
{
    char json[64];
    vrm_format_str(json, sizeof(json), "SmartSolar 150/35");
    TEST_ASSERT_EQUAL_STRING("{\"value\":\"SmartSolar 150/35\"}", json);
}

void test_vrm_payload_str_empty(void)
{
    char json[32];
    vrm_format_str(json, sizeof(json), "");
    TEST_ASSERT_EQUAL_STRING("{\"value\":\"\"}", json);
}

// -- Tests : SOC Estimation ----------------------------------------------

void test_soc_estimation_midrange(void)
{
    // 26.4V entre 24V (empty) et 28.8V (full) = 50%
    float soc = estimate_soc(26.4f, 24.0f, 28.8f);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 50.0f, soc);
}

void test_soc_estimation_full(void)
{
    // 28.8V = plein = 100%
    float soc = estimate_soc(28.8f, 24.0f, 28.8f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, soc);
}

void test_soc_estimation_empty(void)
{
    // 24.0V = vide = 0%
    float soc = estimate_soc(24.0f, 24.0f, 28.8f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, soc);
}

void test_soc_estimation_clamp_below(void)
{
    // 22.0V < 24V (sous-tension) → clampé à 0%
    float soc = estimate_soc(22.0f, 24.0f, 28.8f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, soc);
}

void test_soc_estimation_clamp_above(void)
{
    // 32.0V > 28.8V (sur-tension) → clampé à 100%
    float soc = estimate_soc(32.0f, 24.0f, 28.8f);
    TEST_ASSERT_FLOAT_WITHIN(0.1f, 100.0f, soc);
}

void test_soc_estimation_quarter(void)
{
    // 25.2V = 1/4 entre 24V et 28.8V = 25%
    float soc = estimate_soc(25.2f, 24.0f, 28.8f);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 25.0f, soc);
}

void test_soc_estimation_three_quarter(void)
{
    // 27.6V = 3/4 entre 24V et 28.8V = 75%
    float soc = estimate_soc(27.6f, 24.0f, 28.8f);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 75.0f, soc);
}

// -- Unity setup/teardown -----------------------------------------------

void setUp(void)
{
    // Initialisation avant chaque test (si nécessaire)
}

void tearDown(void)
{
    // Cleanup après chaque test (si nécessaire)
}

// -- Entry point ---------------------------------------------------------

int main(void)
{
    UNITY_BEGIN();

    // Topics
    RUN_TEST(test_vrm_topic_solar_voltage);
    RUN_TEST(test_vrm_topic_battery_soc);
    RUN_TEST(test_vrm_topic_keepalive);
    RUN_TEST(test_vrm_topic_battery_voltage);
    RUN_TEST(test_vrm_topic_battery_current);

    // Payloads float
    RUN_TEST(test_vrm_payload_float);
    RUN_TEST(test_vrm_payload_float_zero);
    RUN_TEST(test_vrm_payload_float_negative);

    // Payloads int
    RUN_TEST(test_vrm_payload_int);
    RUN_TEST(test_vrm_payload_int_zero);
    RUN_TEST(test_vrm_payload_int_negative);

    // Payloads string
    RUN_TEST(test_vrm_payload_str);
    RUN_TEST(test_vrm_payload_str_empty);

    // SOC estimation
    RUN_TEST(test_soc_estimation_midrange);
    RUN_TEST(test_soc_estimation_full);
    RUN_TEST(test_soc_estimation_empty);
    RUN_TEST(test_soc_estimation_clamp_below);
    RUN_TEST(test_soc_estimation_clamp_above);
    RUN_TEST(test_soc_estimation_quarter);
    RUN_TEST(test_soc_estimation_three_quarter);

    return UNITY_END();
}
