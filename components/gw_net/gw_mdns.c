#include "esp_check.h"
#include "mdns.h"
#include "gw_mdns.h"

static const char *TAG = "gw_mdns";

esp_err_t gw_mdns_start(const char *hostname, const char *instance, bool https)
{
    ESP_RETURN_ON_ERROR(mdns_init(), TAG, "init");
    ESP_RETURN_ON_ERROR(mdns_hostname_set(hostname), TAG, "hostname");
    ESP_RETURN_ON_ERROR(mdns_instance_name_set(instance), TAG, "instance");
    ESP_RETURN_ON_ERROR(mdns_service_add(instance, https ? "_https" : "_http", "_tcp",
                                         https ? 443 : 80, NULL, 0), TAG, "service");
    ESP_LOGI(TAG, "%s.local advertised (%s)", hostname, https ? "https" : "http");
    return ESP_OK;
}
