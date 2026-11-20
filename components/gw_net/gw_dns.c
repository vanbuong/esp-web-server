#include "esp_log.h"
#include "esp_netif.h"    // dns_server.h uses esp_ip4_addr_t without including it
#include "dns_server.h"
#include "gw_dns.h"

esp_err_t gw_dns_start(void)
{
    // "*" = answer every query; "WIFI_AP_DEF" = with the IP of the default Wi-Fi AP interface.
    dns_server_config_t cfg = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
    if (!start_dns_server(&cfg)) {
        ESP_LOGE("gw_dns", "DNS server failed to start");
        return ESP_FAIL;
    }
    return ESP_OK;
}
