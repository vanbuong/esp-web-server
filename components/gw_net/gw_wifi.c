#include <string.h>
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "gw_wifi.h"

static const char *TAG = "gw_wifi";

#define TEST_TIMEOUT_US (20 * 1000 * 1000)
#define SCAN_RECORDS_MAX 32

// Test state is written from the Wi-Fi event task and the timer task, read from HTTP handlers.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static gw_wifi_test_state_t s_state = GW_WIFI_TEST_IDLE;
static const char *s_reason = "";
static esp_timer_handle_t s_timer;

static void finish(gw_wifi_test_state_t st, const char *reason)
{
    bool was_running;
    portENTER_CRITICAL(&s_lock);
    was_running = (s_state == GW_WIFI_TEST_RUNNING);
    if (was_running) {
        s_state = st;
        s_reason = reason;
    }
    portEXIT_CRITICAL(&s_lock);
    if (!was_running) {
        return;
    }
    esp_timer_stop(s_timer);   // ESP_ERR_INVALID_STATE if it already fired: fine
    if (st == GW_WIFI_TEST_FAILED) {
        esp_wifi_disconnect();  // stop trying; the user picks again
    }
    ESP_LOGI(TAG, "test-connect %s %s", st == GW_WIFI_TEST_OK ? "ok" : "failed", reason);
}

static const char *reason_name(uint8_t r)
{
    switch (r) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "auth_failed";   // almost always a wrong password
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "no_ap_found";
    default:
        return "other";
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = data;
        if (d->reason == WIFI_REASON_ASSOC_LEAVE) {
            return;   // our own esp_wifi_disconnect() before a new test, not a failure
        }
        finish(GW_WIFI_TEST_FAILED, reason_name(d->reason));
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        finish(GW_WIFI_TEST_OK, "");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "setup client connected");
    }
}

static void on_timeout(void *arg)
{
    finish(GW_WIFI_TEST_FAILED, "timeout");
}

esp_err_t gw_wifi_start_setup(const char *ap_ssid, const char *ap_password, uint8_t channel)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t err = esp_event_loop_create_default();
    ESP_RETURN_ON_FALSE(err == ESP_OK || err == ESP_ERR_INVALID_STATE, err, TAG, "event loop");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_ap(), ESP_FAIL, TAG, "ap netif");
    ESP_RETURN_ON_FALSE(esp_netif_create_default_wifi_sta(), ESP_FAIL, TAG, "sta netif");

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    // Credentials live in our own NVS keys (gw_store), not in the Wi-Fi driver's copy.
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL), TAG, "evt");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL), TAG, "evt");

    const esp_timer_create_args_t targs = {.callback = on_timeout, .name = "wifi_test"};
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_timer), TAG, "timer");

    wifi_config_t ap = {
        .ap = {
            .channel = channel,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,   // never open: setup traffic is plain HTTP
            .pmf_cfg = {.required = false},
        },
    };
    strlcpy((char *)ap.ap.ssid, ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen((char *)ap.ap.ssid);
    strlcpy((char *)ap.ap.password, ap_password, sizeof(ap.ap.password));

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "ap config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    ESP_LOGI(TAG, "setup AP \"%s\" up on channel %u", ap_ssid, channel);
    return ESP_OK;
}

static gw_auth_mode_t map_auth(wifi_auth_mode_t a)
{
    switch (a) {
    case WIFI_AUTH_OPEN: return GW_AUTH_OPEN;
    case WIFI_AUTH_WEP: return GW_AUTH_WEP;
    case WIFI_AUTH_WPA_PSK: return GW_AUTH_WPA;
    case WIFI_AUTH_WPA2_PSK:
    case WIFI_AUTH_WPA_WPA2_PSK: return GW_AUTH_WPA2;
    case WIFI_AUTH_WPA3_PSK:
    case WIFI_AUTH_WPA2_WPA3_PSK: return GW_AUTH_WPA3;
    case WIFI_AUTH_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENT_192:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA_ENTERPRISE: return GW_AUTH_ENTERPRISE;
    default: return GW_AUTH_OTHER;
    }
}

esp_err_t gw_wifi_scan(gw_ap_info_t *out, size_t max, size_t *count)
{
    *count = 0;
    ESP_RETURN_ON_FALSE(gw_wifi_test_get(NULL, 0) != GW_WIFI_TEST_RUNNING, ESP_ERR_INVALID_STATE,
                        TAG, "scan during test");
    wifi_scan_config_t cfg = {0};   // all channels, active scan, include hidden
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&cfg, true), TAG, "scan");

    uint16_t n = SCAN_RECORDS_MAX;
    wifi_ap_record_t *rec = calloc(n, sizeof(*rec));
    gw_ap_info_t *all = calloc(n, sizeof(*all));
    if (!rec || !all) {
        free(rec);
        free(all);
        esp_wifi_clear_ap_list();
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_wifi_scan_get_ap_records(&n, rec);   // also frees the driver's list
    if (err == ESP_OK) {
        for (size_t i = 0; i < n; i++) {
            strlcpy(all[i].ssid, (const char *)rec[i].ssid, sizeof(all[i].ssid));
            all[i].rssi = rec[i].rssi;
            all[i].auth = map_auth(rec[i].authmode);
            all[i].channel = rec[i].primary;
        }
        // Normalize everything first, then cap: capping first could drop the strongest networks.
        size_t m = gw_ap_list_normalize(all, n);
        *count = m < max ? m : max;
        memcpy(out, all, *count * sizeof(*out));
    }
    free(rec);
    free(all);
    return err;
}

esp_err_t gw_wifi_test_start(const char *ssid, const char *pass)
{
    portENTER_CRITICAL(&s_lock);
    bool busy = (s_state == GW_WIFI_TEST_RUNNING);
    if (!busy) {
        s_state = GW_WIFI_TEST_RUNNING;
        s_reason = "";
    }
    portEXIT_CRITICAL(&s_lock);
    if (busy) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t sta = {0};
    strlcpy((char *)sta.sta.ssid, ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, pass, sizeof(sta.sta.password));
    esp_wifi_disconnect();   // leave any previous test network (reason ASSOC_LEAVE, ignored)
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &sta);
    if (err == ESP_OK) {
        err = esp_timer_start_once(s_timer, TEST_TIMEOUT_US);
    }
    if (err == ESP_OK) {
        err = esp_wifi_connect();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "test start: %s", esp_err_to_name(err));
        finish(GW_WIFI_TEST_FAILED, "other");
    }
    return ESP_OK;   // started; the outcome is reported through gw_wifi_test_get()
}

gw_wifi_test_state_t gw_wifi_test_get(char *reason, size_t reason_len)
{
    portENTER_CRITICAL(&s_lock);
    gw_wifi_test_state_t st = s_state;
    const char *r = s_reason;
    portEXIT_CRITICAL(&s_lock);
    if (reason && reason_len) {
        strlcpy(reason, r, reason_len);
    }
    return st;
}
