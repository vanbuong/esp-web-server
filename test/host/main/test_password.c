#include <string.h>
#include "unity.h"
#include "gw_password.h"

static void hex_to_bytes(const char *hex, uint8_t *out)
{
    for (size_t i = 0; i < strlen(hex) / 2; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static void test_pbkdf2_vector_c1(void)
{
    uint8_t out[GW_PW_HASH_LEN], want[GW_PW_HASH_LEN];
    hex_to_bytes("120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b", want);
    TEST_ASSERT_EQUAL(ESP_OK, gw_pw_hash("password", (const uint8_t *)"salt", 4, 1, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, GW_PW_HASH_LEN);
}

static void test_pbkdf2_vector_c2(void)
{
    uint8_t out[GW_PW_HASH_LEN], want[GW_PW_HASH_LEN];
    hex_to_bytes("ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43", want);
    TEST_ASSERT_EQUAL(ESP_OK, gw_pw_hash("password", (const uint8_t *)"salt", 4, 2, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(want, out, GW_PW_HASH_LEN);
}

static void test_verify_ok_and_wrong(void)
{
    uint8_t salt[GW_PW_SALT_LEN], hash[GW_PW_HASH_LEN], out[GW_PW_HASH_LEN];
    for (int i = 0; i < GW_PW_SALT_LEN; i++) {
        salt[i] = (uint8_t)i;
    }
    hex_to_bytes("9244f4657e05d9e3182b7ca6b07ae31c0a51ec75c1326ac25d8ae420929f8888", hash);
    TEST_ASSERT_EQUAL(ESP_OK, gw_pw_hash("admin123", salt, sizeof(salt), 10, out));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(hash, out, GW_PW_HASH_LEN);
    TEST_ASSERT_TRUE(gw_pw_verify("admin123", salt, hash, 10));
    TEST_ASSERT_FALSE(gw_pw_verify("admin124", salt, hash, 10));
    TEST_ASSERT_FALSE(gw_pw_verify("", salt, hash, 10));
}

static void test_policy_bounds(void)
{
    char buf[70];
    memset(buf, 'a', sizeof(buf));
    buf[7] = 0;  TEST_ASSERT_FALSE(gw_pw_policy_ok(buf));
    buf[7] = 'a'; buf[8] = 0;  TEST_ASSERT_TRUE(gw_pw_policy_ok(buf));
    buf[8] = 'a'; buf[64] = 0; TEST_ASSERT_TRUE(gw_pw_policy_ok(buf));
    buf[64] = 'a'; buf[65] = 0; TEST_ASSERT_FALSE(gw_pw_policy_ok(buf));
    TEST_ASSERT_FALSE(gw_pw_policy_ok(NULL));
    TEST_ASSERT_TRUE(gw_pw_policy_ok("p\xc3\xa4ssw\xc3\xb6rd"));   // "pässwörd", 10 bytes
}

void run_password_tests(void)
{
    RUN_TEST(test_pbkdf2_vector_c1);
    RUN_TEST(test_pbkdf2_vector_c2);
    RUN_TEST(test_verify_ok_and_wrong);
    RUN_TEST(test_policy_bounds);
}
