#include <time.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "gw_time.h"

static const char *TAG = "gw_time";

static void on_sync(struct timeval *tv)
{
    char buf[32];
    struct tm tm;
    gmtime_r(&tv->tv_sec, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &tm);
    ESP_LOGI(TAG, "time synced: %s", buf);
}

esp_err_t gw_time_start(const char *server)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
    cfg.sync_cb = on_sync;
    return esp_netif_sntp_init(&cfg);
}

bool gw_time_valid(void)
{
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    return tm.tm_year + 1900 >= 2025;
}
