#include <string.h>
#include <ctype.h>
#include "unity.h"
#include "gw_auth.h"

static uint8_t s_counter;
static void fake_rand(void *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        ((uint8_t *)buf)[i] = s_counter++;
    }
}

static gw_creds_t default_creds(void)
{
    gw_creds_t c = {.user = "admin", .default_password = "admin", .pw_set = false, .iterations = 10};
    return c;
}

static gw_creds_t set_creds(const char *pw)
{
    gw_creds_t c = default_creds();
    c.pw_set = true;
    for (int i = 0; i < GW_PW_SALT_LEN; i++) {
        c.salt[i] = (uint8_t)(i * 7);
    }
    TEST_ASSERT_EQUAL(ESP_OK, gw_pw_hash(pw, c.salt, GW_PW_SALT_LEN, c.iterations, c.hash));
    return c;
}

static void test_login_default_before_set(void)
{
    gw_auth_t *a = gw_auth_create(4, 900, fake_rand);
    gw_creds_t c = default_creds();
    char tok[GW_TOKEN_HEX_LEN + 1];
    TEST_ASSERT_EQUAL(GW_LOGIN_OK, gw_auth_login(a, &c, "admin", "admin", 100, tok, NULL));
    TEST_ASSERT_EQUAL(GW_TOKEN_HEX_LEN, strlen(tok));
    for (int i = 0; i < GW_TOKEN_HEX_LEN; i++) {
        TEST_ASSERT_TRUE(isxdigit((unsigned char)tok[i]));
    }
    TEST_ASSERT_TRUE(gw_auth_check(a, tok, 101));
    gw_auth_destroy(a);
}

static void test_default_rejected_after_set(void)
{
    gw_auth_t *a = gw_auth_create(4, 900, fake_rand);
    gw_creds_t c = set_creds("newpass123");
    char tok[GW_TOKEN_HEX_LEN + 1];
    TEST_ASSERT_EQUAL(GW_LOGIN_BAD, gw_auth_login(a, &c, "admin", "admin", 100, tok, NULL));
    TEST_ASSERT_EQUAL(GW_LOGIN_OK, gw_auth_login(a, &c, "admin", "newpass123", 100, tok, NULL));
    gw_auth_destroy(a);
}

static void test_wrong_user(void)
{
    gw_auth_t *a = gw_auth_create(4, 900, fake_rand);
    gw_creds_t c = default_creds();
    char tok[GW_TOKEN_HEX_LEN + 1];
    TEST_ASSERT_EQUAL(GW_LOGIN_BAD, gw_auth_login(a, &c, "root", "admin", 100, tok, NULL));
    gw_auth_destroy(a);
}

static void test_lockout_after_5(void)
{
    gw_auth_t *a = gw_auth_create(4, 900, fake_rand);
    gw_creds_t c = default_creds();
    char tok[GW_TOKEN_HEX_LEN + 1];
    uint32_t retry = 0;
    for (int i = 0; i < 5; i++) {
        TEST_ASSERT_EQUAL(GW_LOGIN_BAD, gw_auth_login(a, &c, "admin", "nope", 100, tok, NULL));
    }
    TEST_ASSERT_EQUAL(GW_LOGIN_LOCKED, gw_auth_login(a, &c, "admin", "admin", 101, tok, &retry));
    TEST_ASSERT_EQUAL_UINT32(29, retry);
    TEST_ASSERT_EQUAL(GW_LOGIN_OK, gw_auth_login(a, &c, "admin", "admin", 130, tok, NULL));
    gw_auth_destroy(a);
}

static void test_session_idle_timeout(void)
{
    gw_auth_t *a = gw_auth_create(4, 900, fake_rand);
    gw_creds_t c = default_creds();
    char tok[GW_TOKEN_HEX_LEN + 1];
    const uint32_t t = 1000;
    TEST_ASSERT_EQUAL(GW_LOGIN_OK, gw_auth_login(a, &c, "admin", "admin", t, tok, NULL));
    TEST_ASSERT_TRUE(gw_auth_check(a, tok, t + 899));          // refreshes
    TEST_ASSERT_TRUE(gw_auth_check(a, tok, t + 899 + 900));    // exactly at the limit
    TEST_ASSERT_FALSE(gw_auth_check(a, tok, t + 899 + 900 + 901));
    gw_auth_destroy(a);
}

static void test_token_rejects(void)
{
    gw_auth_t *a = gw_auth_create(4, 900, fake_rand);
    gw_creds_t c = default_creds();
    char tok[GW_TOKEN_HEX_LEN + 1];
    TEST_ASSERT_EQUAL(GW_LOGIN_OK, gw_auth_login(a, &c, "admin", "admin", 100, tok, NULL));
    char bad[GW_TOKEN_HEX_LEN + 2];
    TEST_ASSERT_FALSE(gw_auth_check(a, NULL, 100));
    TEST_ASSERT_FALSE(gw_auth_check(a, "", 100));
    memcpy(bad, tok, 63); bad[63] = 0;
    TEST_ASSERT_FALSE(gw_auth_check(a, bad, 100));
    memcpy(bad, tok, 64); bad[64] = '0'; bad[65] = 0;
    TEST_ASSERT_FALSE(gw_auth_check(a, bad, 100));
    memset(bad, 'f', 64); bad[64] = 0;
    TEST_ASSERT_FALSE(gw_auth_check(a, bad, 100));
    gw_auth_destroy(a);
}

static void test_lru_eviction(void)
{
    gw_auth_t *a = gw_auth_create(2, 900, fake_rand);
    gw_creds_t c = default_creds();
    char ta[GW_TOKEN_HEX_LEN + 1], tb[GW_TOKEN_HEX_LEN + 1], tc[GW_TOKEN_HEX_LEN + 1];
    TEST_ASSERT_EQUAL(GW_LOGIN_OK, gw_auth_login(a, &c, "admin", "admin", 1, ta, NULL));
    TEST_ASSERT_EQUAL(GW_LOGIN_OK, gw_auth_login(a, &c, "admin", "admin", 2, tb, NULL));
    TEST_ASSERT_TRUE(gw_auth_check(a, ta, 3));
    TEST_ASSERT_EQUAL(GW_LOGIN_OK, gw_auth_login(a, &c, "admin", "admin", 4, tc, NULL));
    TEST_ASSERT_TRUE(gw_auth_check(a, ta, 5));
    TEST_ASSERT_FALSE(gw_auth_check(a, tb, 5));
    TEST_ASSERT_TRUE(gw_auth_check(a, tc, 5));
    gw_auth_destroy(a);
}

static void test_logout_all(void)
{
    gw_auth_t *a = gw_auth_create(4, 900, fake_rand);
    gw_creds_t c = default_creds();
    char ta[GW_TOKEN_HEX_LEN + 1], tb[GW_TOKEN_HEX_LEN + 1];
    TEST_ASSERT_EQUAL(GW_LOGIN_OK, gw_auth_login(a, &c, "admin", "admin", 1, ta, NULL));
    TEST_ASSERT_EQUAL(GW_LOGIN_OK, gw_auth_login(a, &c, "admin", "admin", 2, tb, NULL));
    gw_auth_logout(a, ta);
    TEST_ASSERT_FALSE(gw_auth_check(a, ta, 3));
    TEST_ASSERT_TRUE(gw_auth_check(a, tb, 3));
    gw_auth_logout_all(a);
    TEST_ASSERT_FALSE(gw_auth_check(a, tb, 3));
    gw_auth_destroy(a);
}

static void test_cookie_parse(void)
{
    char out[16];
    TEST_ASSERT_TRUE(gw_cookie_get("a=1; gw_session=abc; b=2", "gw_session", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("abc", out);
    TEST_ASSERT_TRUE(gw_cookie_get("gw_session=xyz", "gw_session", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("xyz", out);
    TEST_ASSERT_FALSE(gw_cookie_get("gw_session_x=1", "gw_session", out, sizeof(out)));
    TEST_ASSERT_FALSE(gw_cookie_get("my_gw_session=1", "gw_session", out, sizeof(out)));
    TEST_ASSERT_FALSE(gw_cookie_get(NULL, "gw_session", out, sizeof(out)));
    char tiny[3];
    TEST_ASSERT_FALSE(gw_cookie_get("gw_session=abc", "gw_session", tiny, sizeof(tiny)));
}

void run_auth_tests(void)
{
    RUN_TEST(test_login_default_before_set);
    RUN_TEST(test_default_rejected_after_set);
    RUN_TEST(test_wrong_user);
    RUN_TEST(test_lockout_after_5);
    RUN_TEST(test_session_idle_timeout);
    RUN_TEST(test_token_rejects);
    RUN_TEST(test_lru_eviction);
    RUN_TEST(test_logout_all);
    RUN_TEST(test_cookie_parse);
}
