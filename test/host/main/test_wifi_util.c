#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "cJSON.h"
#include "gw_wifi_util.h"

static gw_ap_info_t ap(const char *ssid, int8_t rssi)
{
    gw_ap_info_t a = {.rssi = rssi, .auth = GW_AUTH_WPA2, .channel = 6};
    strncpy(a.ssid, ssid, sizeof(a.ssid) - 1);
    return a;
}

static void test_normalize(void)
{
    gw_ap_info_t list[] = {ap("a", -70), ap("b", -40), ap("a", -50), ap("", -30)};
    size_t n = gw_ap_list_normalize(list, 4);
    TEST_ASSERT_EQUAL(2, n);
    TEST_ASSERT_EQUAL_STRING("b", list[0].ssid);
    TEST_ASSERT_EQUAL(-40, list[0].rssi);
    TEST_ASSERT_EQUAL_STRING("a", list[1].ssid);
    TEST_ASSERT_EQUAL(-50, list[1].rssi);
}

static void test_normalize_caps_at_20(void)
{
    gw_ap_info_t list[25];
    for (int i = 0; i < 25; i++) {
        char name[8];
        snprintf(name, sizeof(name), "n%d", i);
        list[i] = ap(name, (int8_t)(-90 + i));   // n24 strongest (-66)
    }
    size_t n = gw_ap_list_normalize(list, 25);
    TEST_ASSERT_EQUAL(GW_SCAN_MAX, n);
    TEST_ASSERT_EQUAL_STRING("n24", list[0].ssid);
    TEST_ASSERT_EQUAL(-85, list[19].rssi);   // n5: the 5 weakest were dropped
}

static void test_json_escapes(void)
{
    gw_ap_info_t list[4] = {ap("he said \"hi\"", -40), ap("back\\slash", -50),
                            ap("12345678901234567890123456789012", -60), ap("", -70)};
    list[3].ssid[0] = (char)0xC3;   // invalid UTF-8 sequence
    list[3].ssid[1] = (char)0x28;
    list[3].ssid[2] = 0;
    list[0].auth = GW_AUTH_OPEN;
    char *json = gw_ap_list_json(list, 4);
    TEST_ASSERT_NOT_NULL(json);
    cJSON *root = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL(root);
    TEST_ASSERT_EQUAL(4, cJSON_GetArraySize(root));
    cJSON *first = cJSON_GetArrayItem(root, 0);
    TEST_ASSERT_EQUAL_STRING("he said \"hi\"", cJSON_GetObjectItem(first, "ssid")->valuestring);
    TEST_ASSERT_EQUAL_STRING("open", cJSON_GetObjectItem(first, "auth")->valuestring);
    TEST_ASSERT_EQUAL(-40, cJSON_GetObjectItem(first, "rssi")->valueint);
    TEST_ASSERT_EQUAL(6, cJSON_GetObjectItem(first, "channel")->valueint);
    TEST_ASSERT_EQUAL_STRING("12345678901234567890123456789012",
                             cJSON_GetObjectItem(cJSON_GetArrayItem(root, 2), "ssid")->valuestring);
    cJSON_Delete(root);
    free(json);
}

static void test_creds_valid(void)
{
    char s33[34], p[70];
    memset(s33, 's', 33); s33[33] = 0;
    TEST_ASSERT_FALSE(gw_wifi_creds_valid("", "12345678"));
    TEST_ASSERT_FALSE(gw_wifi_creds_valid(s33, "12345678"));
    TEST_ASSERT_FALSE(gw_wifi_creds_valid(NULL, ""));
    TEST_ASSERT_FALSE(gw_wifi_creds_valid("net", NULL));
    TEST_ASSERT_TRUE(gw_wifi_creds_valid("net", ""));
    TEST_ASSERT_FALSE(gw_wifi_creds_valid("net", "1234567"));
    TEST_ASSERT_TRUE(gw_wifi_creds_valid("net", "12345678"));
    memset(p, 'x', 63); p[63] = 0;
    TEST_ASSERT_TRUE(gw_wifi_creds_valid("net", p));
    memset(p, 'a', 64); p[64] = 0;           // 64 hex digits: raw PSK
    TEST_ASSERT_TRUE(gw_wifi_creds_valid("net", p));
    p[10] = 'g';                             // 64 chars, not hex
    TEST_ASSERT_FALSE(gw_wifi_creds_valid("net", p));
}

static void test_ap_pw(void)
{
    uint8_t rnd[GW_AP_PW_LEN];
    char out[GW_AP_PW_LEN + 1];
    for (int i = 0; i < GW_AP_PW_LEN; i++) {
        rnd[i] = (uint8_t)i;
    }
    gw_ap_pw_from_random(rnd, out);
    TEST_ASSERT_EQUAL_STRING("ABCDEFGHJKLM", out);
    memset(rnd, 0xFF, sizeof(rnd));
    gw_ap_pw_from_random(rnd, out);
    TEST_ASSERT_EQUAL_STRING("999999999999", out);
    TEST_ASSERT_EQUAL(GW_AP_PW_LEN, strlen(out));
}

void run_wifi_util_tests(void)
{
    RUN_TEST(test_normalize);
    RUN_TEST(test_normalize_caps_at_20);
    RUN_TEST(test_json_escapes);
    RUN_TEST(test_creds_valid);
    RUN_TEST(test_ap_pw);
}
