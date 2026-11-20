#include "unity.h"
#include "gw_dev_id.h"

static const uint8_t MAC[6] = {0x24, 0x6F, 0x28, 0xAB, 0x1A, 0x2B};

static void test_dev_id_4_digits(void)
{
    char id[8];
    TEST_ASSERT_EQUAL(ESP_OK, gw_dev_id_format(MAC, 4, id, sizeof(id)));
    TEST_ASSERT_EQUAL_STRING("1A2B", id);
}

static void test_dev_id_6_digits(void)
{
    char id[8];
    TEST_ASSERT_EQUAL(ESP_OK, gw_dev_id_format(MAC, 6, id, sizeof(id)));
    TEST_ASSERT_EQUAL_STRING("AB1A2B", id);
}

static void test_dev_id_rejects_5_digits(void)
{
    char id[8];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, gw_dev_id_format(MAC, 5, id, sizeof(id)));
}

static void test_dev_id_buffer_too_small(void)
{
    char id[4];   // needs 5 for "1A2B" + NUL
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, gw_dev_id_format(MAC, 4, id, sizeof(id)));
}

static void test_name_join(void)
{
    char out[32];
    TEST_ASSERT_EQUAL(ESP_OK, gw_name_join("gateway", "1A2B", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("gateway1A2B", out);
    char small[10];
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_SIZE, gw_name_join("Gateway-Setup-", "1A2B", small, sizeof(small)));
}

void run_dev_id_tests(void)
{
    RUN_TEST(test_dev_id_4_digits);
    RUN_TEST(test_dev_id_6_digits);
    RUN_TEST(test_dev_id_rejects_5_digits);
    RUN_TEST(test_dev_id_buffer_too_small);
    RUN_TEST(test_name_join);
}
