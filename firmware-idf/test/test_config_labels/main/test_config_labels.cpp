#ifdef NATIVE_TEST

#include "unity.h"
#include <cstring>
#include <cstdio>

#define BMU_MAX_BATTERIES       16
#define BMU_CONFIG_BATLABEL_MAX  9

static char s_labels[BMU_MAX_BATTERIES][BMU_CONFIG_BATLABEL_MAX];

static void init_defaults(void)
{
    for (int i = 0; i < BMU_MAX_BATTERIES; i++)
        snprintf(s_labels[i], BMU_CONFIG_BATLABEL_MAX, "B%d", i + 1);
}

static int set_label(int idx, const char *label)
{
    if (idx < 0 || idx >= BMU_MAX_BATTERIES) return -1;
    if (label == NULL || label[0] == '\0') return -1;
    strncpy(s_labels[idx], label, BMU_CONFIG_BATLABEL_MAX - 1);
    s_labels[idx][BMU_CONFIG_BATLABEL_MAX - 1] = '\0';
    return 0;
}

static const char *get_label(int idx)
{
    if (idx < 0 || idx >= BMU_MAX_BATTERIES) return "?";
    return s_labels[idx];
}

static int parse_labels_from_buffer(const char *buf)
{
    init_defaults();
    int idx = 0;
    const char *p = buf;
    while (idx < BMU_MAX_BATTERIES && *p != '\0') {
        const char *eol = strchr(p, '\n');
        if (eol == NULL) eol = p + strlen(p);
        int len = (int)(eol - p);
        if (len > 0 && *(eol - 1) == '\r') len--;
        if (len > 0) {
            if (len > BMU_CONFIG_BATLABEL_MAX - 1) len = BMU_CONFIG_BATLABEL_MAX - 1;
            memcpy(s_labels[idx], p, len);
            s_labels[idx][len] = '\0';
        }
        idx++;
        p = (*eol == '\n') ? eol + 1 : eol;
    }
    return idx;
}

void test_defaults(void)
{
    init_defaults();
    TEST_ASSERT_EQUAL_STRING("B1", get_label(0));
    TEST_ASSERT_EQUAL_STRING("B16", get_label(15));
}

void test_set_valid(void)
{
    init_defaults();
    TEST_ASSERT_EQUAL_INT(0, set_label(0, "Tender"));
    TEST_ASSERT_EQUAL_STRING("Tender", get_label(0));
}

void test_set_truncation(void)
{
    init_defaults();
    set_label(0, "VeryLongBatteryName");
    TEST_ASSERT_EQUAL_INT(8, (int)strlen(get_label(0)));
}

void test_set_invalid_idx_negative(void) { TEST_ASSERT_EQUAL_INT(-1, set_label(-1, "X")); }
void test_set_invalid_idx_overflow(void) { TEST_ASSERT_EQUAL_INT(-1, set_label(16, "X")); }
void test_set_empty_label(void) { TEST_ASSERT_EQUAL_INT(-1, set_label(0, "")); }
void test_set_null_label(void) { TEST_ASSERT_EQUAL_INT(-1, set_label(0, NULL)); }

void test_get_invalid_idx(void)
{
    TEST_ASSERT_EQUAL_STRING("?", get_label(-1));
    TEST_ASSERT_EQUAL_STRING("?", get_label(16));
}

void test_parse_file_basic(void)
{
    int n = parse_labels_from_buffer("Tender\nLED1\nLED2\nCabine\n");
    TEST_ASSERT_EQUAL_INT(4, n);
    TEST_ASSERT_EQUAL_STRING("Tender", get_label(0));
    TEST_ASSERT_EQUAL_STRING("LED1", get_label(1));
    TEST_ASSERT_EQUAL_STRING("Cabine", get_label(3));
    TEST_ASSERT_EQUAL_STRING("B5", get_label(4));
}

void test_parse_file_empty_lines(void)
{
    parse_labels_from_buffer("Tender\n\nLED2\n");
    TEST_ASSERT_EQUAL_STRING("Tender", get_label(0));
    TEST_ASSERT_EQUAL_STRING("B2", get_label(1));
    TEST_ASSERT_EQUAL_STRING("LED2", get_label(2));
}

void test_parse_file_crlf(void)
{
    parse_labels_from_buffer("Tender\r\nLED1\r\n");
    TEST_ASSERT_EQUAL_STRING("Tender", get_label(0));
    TEST_ASSERT_EQUAL_STRING("LED1", get_label(1));
}

void test_parse_file_overflow(void)
{
    char buf[512] = {};
    for (int i = 0; i < 20; i++) {
        char line[16];
        snprintf(line, sizeof(line), "BAT%d\n", i);
        strcat(buf, line);
    }
    int n = parse_labels_from_buffer(buf);
    TEST_ASSERT_EQUAL_INT(16, n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_set_valid);
    RUN_TEST(test_set_truncation);
    RUN_TEST(test_set_invalid_idx_negative);
    RUN_TEST(test_set_invalid_idx_overflow);
    RUN_TEST(test_set_empty_label);
    RUN_TEST(test_set_null_label);
    RUN_TEST(test_get_invalid_idx);
    RUN_TEST(test_parse_file_basic);
    RUN_TEST(test_parse_file_empty_lines);
    RUN_TEST(test_parse_file_crlf);
    RUN_TEST(test_parse_file_overflow);
    return UNITY_END();
}

#endif
