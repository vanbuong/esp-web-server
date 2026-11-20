#pragma once
// Pure Wi-Fi helpers: cleaning up scan results for the UI, validating credentials, and making
// the per-device setup password. The radio code (gw_net) maps esp_wifi types onto these.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    GW_AUTH_OPEN, GW_AUTH_WEP, GW_AUTH_WPA, GW_AUTH_WPA2, GW_AUTH_WPA3, GW_AUTH_ENTERPRISE, GW_AUTH_OTHER
} gw_auth_mode_t;

typedef struct {
    char ssid[33];       // 32 bytes max + NUL; may contain any bytes, not only UTF-8
    int8_t rssi;         // dBm, higher (closer to 0) is stronger
    gw_auth_mode_t auth;
    uint8_t channel;
} gw_ap_info_t;

#define GW_SCAN_MAX 20

/**
 * In place: drops hidden networks (empty SSID), keeps only the strongest entry per SSID
 * (one network often has several access points), sorts strongest first, caps at GW_SCAN_MAX.
 * @return the new count.
 */
size_t gw_ap_list_normalize(gw_ap_info_t *aps, size_t n);

/**
 * JSON array [{"ssid":..,"rssi":..,"auth":"wpa2","channel":..}], malloc'd (caller frees),
 * NULL on allocation failure. auth strings: open, wep, wpa, wpa2, wpa3, enterprise, other.
 */
char *gw_ap_list_json(const gw_ap_info_t *aps, size_t n);

/** SSID 1..32 bytes; password "" (open network), 8..63 bytes (passphrase) or 64 hex digits (raw PSK). */
bool gw_wifi_creds_valid(const char *ssid, const char *pass);

#define GW_AP_PW_LEN 12
/**
 * Setup-network password from 12 random bytes: alphabet[rnd[i] & 31] with a 32-character
 * alphabet without look-alikes (no I, O, 0, 1), so masking with 31 adds no bias.
 */
void gw_ap_pw_from_random(const uint8_t rnd[GW_AP_PW_LEN], char out[GW_AP_PW_LEN + 1]);
