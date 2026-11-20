#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "gw_wifi_util.h"

static int by_rssi_desc(const void *x, const void *y)
{
    return ((const gw_ap_info_t *)y)->rssi - ((const gw_ap_info_t *)x)->rssi;
}

size_t gw_ap_list_normalize(gw_ap_info_t *aps, size_t n)
{
    size_t out = 0;
    for (size_t i = 0; i < n; i++) {
        if (aps[i].ssid[0] == 0) {
            continue;   // hidden network: entered by hand in the UI
        }
        size_t j = 0;
        while (j < out && strcmp(aps[j].ssid, aps[i].ssid) != 0) {
            j++;
        }
        if (j == out) {
            aps[out++] = aps[i];
        } else if (aps[i].rssi > aps[j].rssi) {
            aps[j] = aps[i];
        }
    }
    qsort(aps, out, sizeof(*aps), by_rssi_desc);
    return out < GW_SCAN_MAX ? out : GW_SCAN_MAX;
}

static const char *auth_name(gw_auth_mode_t a)
{
    static const char *const names[] = {"open", "wep", "wpa", "wpa2", "wpa3", "enterprise", "other"};
    return (unsigned)a <= GW_AUTH_OTHER ? names[a] : "other";
}

char *gw_ap_list_json(const gw_ap_info_t *aps, size_t n)
{
    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        return NULL;
    }
    for (size_t i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        if (!o || !cJSON_AddItemToArray(arr, o) ||
                !cJSON_AddStringToObject(o, "ssid", aps[i].ssid) ||
                !cJSON_AddNumberToObject(o, "rssi", aps[i].rssi) ||
                !cJSON_AddStringToObject(o, "auth", auth_name(aps[i].auth)) ||
                !cJSON_AddNumberToObject(o, "channel", aps[i].channel)) {
            cJSON_Delete(arr);   // also frees o once it was added
            return NULL;
        }
    }
    char *s = cJSON_PrintUnformatted(arr);   // escapes quotes, backslashes and control chars
    cJSON_Delete(arr);
    return s;
}

bool gw_wifi_creds_valid(const char *ssid, const char *pass)
{
    if (!ssid || !pass) {
        return false;
    }
    size_t s = strnlen(ssid, 33), p = strnlen(pass, 65);
    if (s < 1 || s > 32) {
        return false;
    }
    if (p == 0 || (p >= 8 && p <= 63)) {
        return true;
    }
    if (p == 64) {   // raw 256-bit PSK written as hex
        for (size_t i = 0; i < 64; i++) {
            if (!isxdigit((unsigned char)pass[i])) {
                return false;
            }
        }
        return true;
    }
    return false;
}

void gw_ap_pw_from_random(const uint8_t rnd[GW_AP_PW_LEN], char out[GW_AP_PW_LEN + 1])
{
    static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // 32 chars
    for (int i = 0; i < GW_AP_PW_LEN; i++) {
        out[i] = alphabet[rnd[i] & 31];
    }
    out[GW_AP_PW_LEN] = 0;
}
