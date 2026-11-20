#include <string.h>
#include "unity.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "gw_store.h"

static void fresh(void)
{
    nvs_flash_erase();
    nvs_flash_erase_partition("fctry");
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_init());
}

static void test_fresh_defaults(void)
{
    fresh();
    TEST_ASSERT_EQUAL(GW_STATE_SETUP, gw_store_get_state());
    TEST_ASSERT_EQUAL(GW_STEP_PASSWORD, gw_store_get_step());
    bool set = true;
    uint8_t salt[GW_PW_SALT_LEN], hash[GW_PW_HASH_LEN];
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_get_password(&set, salt, hash));
    TEST_ASSERT_FALSE(set);
    char ssid[33], pass[65];
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gw_store_get_wifi(ssid, pass));
    char ap[GW_AP_PW_LEN + 1];
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gw_store_get_ap_password(ap));
}

static void test_roundtrip_all(void)
{
    fresh();
    uint8_t salt[GW_PW_SALT_LEN], hash[GW_PW_HASH_LEN], s2[GW_PW_SALT_LEN], h2[GW_PW_HASH_LEN];
    for (int i = 0; i < GW_PW_SALT_LEN; i++) salt[i] = (uint8_t)(i + 1);
    for (int i = 0; i < GW_PW_HASH_LEN; i++) hash[i] = (uint8_t)(0xA0 + i);
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_set_state(GW_STATE_PROVISIONED));
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_set_step(GW_STEP_TLS));
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_set_password(salt, hash));
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_set_wifi("home", "secret123"));
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_set_ap_password("ABCDEFGHJKLM"));

    TEST_ASSERT_EQUAL(GW_STATE_PROVISIONED, gw_store_get_state());
    TEST_ASSERT_EQUAL(GW_STEP_TLS, gw_store_get_step());
    bool set = false;
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_get_password(&set, s2, h2));
    TEST_ASSERT_TRUE(set);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(salt, s2, GW_PW_SALT_LEN);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(hash, h2, GW_PW_HASH_LEN);
    char ssid[33], pass[65], ap[GW_AP_PW_LEN + 1];
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_get_wifi(ssid, pass));
    TEST_ASSERT_EQUAL_STRING("home", ssid);
    TEST_ASSERT_EQUAL_STRING("secret123", pass);
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_get_ap_password(ap));
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHJKLM", ap);
}

static void test_invalid_step_sanitized(void)
{
    fresh();
    nvs_handle_t h;
    TEST_ASSERT_EQUAL(ESP_OK, nvs_open("gw", NVS_READWRITE, &h));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_set_u8(h, "setup_step", 9));
    TEST_ASSERT_EQUAL(ESP_OK, nvs_commit(h));
    nvs_close(h);
    TEST_ASSERT_EQUAL(GW_STEP_PASSWORD, gw_store_get_step());
}

static void test_factory_reset_keeps_ap_password(void)
{
    fresh();
    uint8_t salt[GW_PW_SALT_LEN] = {1}, hash[GW_PW_HASH_LEN] = {2};
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_set_state(GW_STATE_PROVISIONED));
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_set_step(GW_STEP_DONE));
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_set_password(salt, hash));
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_set_wifi("home", "secret123"));
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_set_ap_password("ABCDEFGHJKLM"));

    TEST_ASSERT_EQUAL(ESP_OK, gw_store_factory_reset());
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_init());

    TEST_ASSERT_EQUAL(GW_STATE_SETUP, gw_store_get_state());
    TEST_ASSERT_EQUAL(GW_STEP_PASSWORD, gw_store_get_step());
    bool set = true;
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_get_password(&set, salt, hash));
    TEST_ASSERT_FALSE(set);
    char ssid[33], pass[65], ap[GW_AP_PW_LEN + 1];
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, gw_store_get_wifi(ssid, pass));
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_get_ap_password(ap));
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHJKLM", ap);
}

static void test_wifi_max_lengths(void)
{
    fresh();
    char s[33], p[65], s2[33], p2[65];
    memset(s, 'S', 32); s[32] = 0;
    memset(p, 'a', 64); p[64] = 0;
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_set_wifi(s, p));
    TEST_ASSERT_EQUAL(ESP_OK, gw_store_get_wifi(s2, p2));
    TEST_ASSERT_EQUAL_STRING(s, s2);
    TEST_ASSERT_EQUAL_STRING(p, p2);
}

void run_store_tests(void)
{
    RUN_TEST(test_fresh_defaults);
    RUN_TEST(test_roundtrip_all);
    RUN_TEST(test_invalid_step_sanitized);
    RUN_TEST(test_factory_reset_keeps_ap_password);
    RUN_TEST(test_wifi_max_lengths);
}
