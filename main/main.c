#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_mac.h"
#include "gw_dev_id.h"

static const char *TAG = "main";

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
}
