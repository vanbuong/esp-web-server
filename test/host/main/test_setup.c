#include "unity.h"
#include "gw_setup.h"

static void test_cannot_skip(void)
{
    TEST_ASSERT_FALSE(gw_setup_allowed(GW_STEP_PASSWORD, GW_STEP_WIFI));
    TEST_ASSERT_FALSE(gw_setup_allowed(GW_STEP_WIFI, GW_STEP_TLS));
    TEST_ASSERT_TRUE(gw_setup_allowed(GW_STEP_PASSWORD, GW_STEP_PASSWORD));
}

static void test_can_redo_earlier(void)
{
    TEST_ASSERT_TRUE(gw_setup_allowed(GW_STEP_TLS, GW_STEP_WIFI));
    TEST_ASSERT_TRUE(gw_setup_allowed(GW_STEP_TLS, GW_STEP_PASSWORD));
}

static void test_done_allows_nothing(void)
{
    TEST_ASSERT_FALSE(gw_setup_allowed(GW_STEP_DONE, GW_STEP_PASSWORD));
    TEST_ASSERT_FALSE(gw_setup_allowed(GW_STEP_DONE, GW_STEP_DONE));
}

static void test_complete_advances_only_current(void)
{
    TEST_ASSERT_EQUAL(GW_STEP_WIFI, gw_setup_complete(GW_STEP_PASSWORD, GW_STEP_PASSWORD));
    TEST_ASSERT_EQUAL(GW_STEP_WIFI, gw_setup_complete(GW_STEP_WIFI, GW_STEP_PASSWORD));
    TEST_ASSERT_EQUAL(GW_STEP_DONE, gw_setup_complete(GW_STEP_FINISH, GW_STEP_FINISH));
    TEST_ASSERT_EQUAL(GW_STEP_DONE, gw_setup_complete(GW_STEP_DONE, GW_STEP_DONE));
}

static void test_names(void)
{
    TEST_ASSERT_EQUAL_STRING("password", gw_setup_step_name(GW_STEP_PASSWORD));
    TEST_ASSERT_EQUAL_STRING("wifi", gw_setup_step_name(GW_STEP_WIFI));
    TEST_ASSERT_EQUAL_STRING("tls", gw_setup_step_name(GW_STEP_TLS));
    TEST_ASSERT_EQUAL_STRING("finish", gw_setup_step_name(GW_STEP_FINISH));
    TEST_ASSERT_EQUAL_STRING("done", gw_setup_step_name(GW_STEP_DONE));
    TEST_ASSERT_EQUAL_STRING("unknown", gw_setup_step_name((gw_step_t)9));
}

static void test_sanitize(void)
{
    TEST_ASSERT_EQUAL(GW_STEP_PASSWORD, gw_setup_sanitize(7));
    TEST_ASSERT_EQUAL(GW_STEP_TLS, gw_setup_sanitize(2));
    TEST_ASSERT_EQUAL(GW_STEP_DONE, gw_setup_sanitize(4));
}

void run_setup_tests(void)
{
    RUN_TEST(test_cannot_skip);
    RUN_TEST(test_can_redo_earlier);
    RUN_TEST(test_done_allows_nothing);
    RUN_TEST(test_complete_advances_only_current);
    RUN_TEST(test_names);
    RUN_TEST(test_sanitize);
}
