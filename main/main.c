#include <string.h>
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "gw_dev_id.h"
#include "gw_store.h"
#include "gw_wifi.h"
#include "gw_wifi_util.h"

static const char *TAG = "main";

// Setup network password: fixed from Kconfig, or unique per device kept in the "factory"
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

void app_main(void)
{
    ESP_LOGI(TAG, "gateway %s booting", esp_app_get_description()->version);

    // Identity from the factory-programmed Wi-Fi station MAC (unique per chip).
    uint8_t mac[6];
    char id[8], host[48], ssid[33];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    ESP_ERROR_CHECK(gw_dev_id_format(mac, CONFIG_GW_DEVICE_ID_LEN, id, sizeof(id)));
    ESP_ERROR_CHECK(gw_name_join(CONFIG_GW_HOSTNAME_PREFIX, id, host, sizeof(host)));
    ESP_ERROR_CHECK(gw_name_join(CONFIG_GW_AP_SSID_PREFIX, id, ssid, sizeof(ssid)));
    ESP_LOGI(TAG, "id=%s host=%s ssid=%s", id, host, ssid);

    ESP_ERROR_CHECK(gw_store_init());
    if (gw_store_get_state() == GW_STATE_PROVISIONED) {
        ESP_LOGW(TAG, "provisioned: normal (HTTPS) mode arrives in plan 2; nothing to run yet");
        return;
    }

    char ap_pw[AP_PW_BUF];
    ESP_ERROR_CHECK(get_ap_password(ap_pw));
    if (strlen(ap_pw) < 8 || !gw_wifi_creds_valid(ssid, ap_pw)) {
        ESP_LOGE(TAG, "setup network password must be 8-63 characters (WPA2); fix GW_AP_PASSWORD "
                 "in menuconfig. Setup mode not started.");
        return;
    }
    ESP_ERROR_CHECK(gw_wifi_start_setup(ssid, ap_pw, CONFIG_GW_AP_CHANNEL));
}
