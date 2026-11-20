#include <string.h>
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "gw_dev_id.h"
#include "gw_store.h"
#include "gw_wifi.h"
#include "gw_dns.h"
#include "gw_mdns.h"
#include "gw_http_setup.h"
#include "gw_http_admin.h"
#include "gw_time.h"
#include "esp_system.h"
#include "gw_wifi_util.h"

static const char *TAG = "main";

// Setup network password: fixed from Kconfig, or unique per device kept in the "fctry"
// partition (generated on first boot if a manufacturer didn't write one).
#define AP_PW_BUF 64   // fits a 63-char WPA2 passphrase and the 12-char per-device one
static esp_err_t get_ap_password(char out[AP_PW_BUF])
{
#if CONFIG_GW_AP_AUTH_FIXED
    strlcpy(out, CONFIG_GW_AP_PASSWORD, AP_PW_BUF);
    return ESP_OK;
#else
    esp_err_t err = gw_store_get_ap_password(out);
    if (err == ESP_ERR_NOT_FOUND) {
        uint8_t rnd[GW_AP_PW_LEN];
        esp_fill_random(rnd, sizeof(rnd));   // hardware RNG (true random while the radio runs)
        gw_ap_pw_from_random(rnd, out);
        err = gw_store_set_ap_password(out);
    }
    if (err == ESP_OK) {
        // Deliberate exception to "never log passwords" (spec 7.1a): this is how the owner
        // reads the per-device setup password if there is no printed label.
        ESP_LOGW(TAG, "Setup Wi-Fi password: %s", out);
    }
    return err;
#endif
}

static const gw_creds_t s_defaults = {
    .user = CONFIG_GW_DEFAULT_USER,
    .default_password = CONFIG_GW_DEFAULT_PASSWORD,
    .iterations = CONFIG_GW_PBKDF2_ITERATIONS,
};

// static: the web servers keep pointers to these after app_main returns.
static char s_id[8], s_host[48], s_ssid[33], s_instance[48];

static void setup_mode(void)
{
    char ap_pw[AP_PW_BUF];
    ESP_ERROR_CHECK(get_ap_password(ap_pw));
    if (strlen(ap_pw) < 8 || !gw_wifi_creds_valid(s_ssid, ap_pw)) {
        ESP_LOGE(TAG, "setup network password must be 8-63 characters (WPA2); fix GW_AP_PASSWORD "
                 "in menuconfig. Setup mode not started.");
        return;
    }
    ESP_ERROR_CHECK(gw_wifi_start_setup(s_ssid, ap_pw, CONFIG_GW_AP_CHANNEL));
    ESP_ERROR_CHECK(gw_dns_start());
    ESP_ERROR_CHECK(gw_mdns_start(s_host, s_instance, false));
    const gw_http_setup_cfg_t http = {
        .device_id = s_id,
        .hostname = s_host,
        .fw_version = esp_app_get_description()->version,
        .defaults = &s_defaults,
    };
    ESP_ERROR_CHECK(gw_http_setup_start(&http));
}

// Provisioned but something is missing (e.g. NVS damaged): go back to the matching setup step
// rather than run half-configured. Reboots.
static void back_to_setup(gw_step_t step, const char *why)
{
    ESP_LOGE(TAG, "%s — returning to setup mode (step %s)", why, gw_setup_step_name(step));
    gw_store_set_step(step);
    gw_store_set_state(GW_STATE_SETUP);
    esp_restart();
}

static void normal_mode(void)
{
    char wifi_ssid[33], wifi_pass[65];
    static gw_tls_rec_t tls;   // kept for the server's lifetime (it uses these PEM strings)
    if (gw_store_get_wifi(wifi_ssid, wifi_pass) != ESP_OK) {
        back_to_setup(GW_STEP_WIFI, "no saved Wi-Fi");
    }
    if (gw_store_get_tls(&tls) != ESP_OK) {
        back_to_setup(GW_STEP_TLS, "no TLS identity");
    }
    ESP_ERROR_CHECK(gw_wifi_start_station(wifi_ssid, wifi_pass));
    memset(wifi_pass, 0, sizeof(wifi_pass));
    ESP_ERROR_CHECK(gw_time_start(CONFIG_GW_SNTP_SERVER));
    ESP_ERROR_CHECK(gw_mdns_start(s_host, s_instance, true));
    const gw_http_admin_cfg_t admin = {
        .device_id = s_id,
        .hostname = s_host,
        .fw_version = esp_app_get_description()->version,
        .defaults = &s_defaults,
        .key_pem = tls.key,
        .cert_pem = tls.cert,
        .ca_pem = tls.ca,
    };
    if (gw_http_admin_start(&admin) == ESP_ERR_INVALID_STATE) {
        back_to_setup(GW_STEP_TLS, "stored TLS identity no longer valid");
    }
    ESP_ERROR_CHECK(gw_http_redirect_start(s_host));
}

void app_main(void)
{
    ESP_LOGI(TAG, "gateway %s booting", esp_app_get_description()->version);

    // Identity from the factory-programmed Wi-Fi station MAC (unique per chip).
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    ESP_ERROR_CHECK(gw_dev_id_format(mac, CONFIG_GW_DEVICE_ID_LEN, s_id, sizeof(s_id)));
    ESP_ERROR_CHECK(gw_name_join(CONFIG_GW_HOSTNAME_PREFIX, s_id, s_host, sizeof(s_host)));
    ESP_ERROR_CHECK(gw_name_join(CONFIG_GW_AP_SSID_PREFIX, s_id, s_ssid, sizeof(s_ssid)));
    ESP_ERROR_CHECK(gw_name_join("Gateway ", s_id, s_instance, sizeof(s_instance)));
    ESP_LOGI(TAG, "id=%s host=%s ssid=%s", s_id, s_host, s_ssid);

    ESP_ERROR_CHECK(gw_store_init());
    if (gw_store_get_state() == GW_STATE_PROVISIONED) {
        normal_mode();
    } else {
        setup_mode();
    }
}
