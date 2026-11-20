#pragma once
// Wi-Fi for setup mode: the device runs its own access point (AP) for the user's phone/PC and,
// at the same time, a station (STA) that scans and test-connects to the user's network.
// Why APSTA: the setup page must stay reachable while the device tries the new network.
// Note: the ESP32 has one radio, so when the STA joins a network on another channel the AP
// moves to that channel too and clients may drop off briefly — hence the async test below.
#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"
#include "gw_wifi_util.h"

/** Starts APSTA: WPA2-PSK AP (max 4 clients, IP 192.168.4.1) + idle STA. Call once. */
esp_err_t gw_wifi_start_setup(const char *ap_ssid, const char *ap_password, uint8_t channel);

/** Blocking active scan (~2-3 s); results normalized (gw_ap_list_normalize). Fails while a test runs. */
esp_err_t gw_wifi_scan(gw_ap_info_t *out, size_t max, size_t *count);

typedef enum { GW_WIFI_TEST_IDLE, GW_WIFI_TEST_RUNNING, GW_WIFI_TEST_OK, GW_WIFI_TEST_FAILED } gw_wifi_test_state_t;

/**
 * Starts connecting the STA to ssid in the background; returns at once.
 * OK when an IP address is obtained, FAILED on rejection or after 20 s.
 * ESP_ERR_INVALID_STATE if a test is already running.
 */
esp_err_t gw_wifi_test_start(const char *ssid, const char *pass);

/**
 * Normal mode: station only. Connects, and reconnects forever with backoff 1, 2, 4 ... 60 s.
 * Never opens the setup AP. Call once.
 */
esp_err_t gw_wifi_start_station(const char *ssid, const char *pass);

/** Current IPv4 address as text; false (and "") while not connected. */
bool gw_wifi_station_ip(char *ip, size_t len);

/** Signal strength of the connected network in dBm; 0 if not connected. */
int8_t gw_wifi_station_rssi(void);

/** reason (on FAILED): "auth_failed" | "no_ap_found" | "timeout" | "other"; "" otherwise. */
gw_wifi_test_state_t gw_wifi_test_get(char *reason, size_t reason_len);
