#include "esp_log.h"
#include "esp_app_desc.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "gateway %s booting", esp_app_get_description()->version);
}
