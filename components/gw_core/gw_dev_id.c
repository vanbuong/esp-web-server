#include <stdio.h>
#include "gw_dev_id.h"

esp_err_t gw_dev_id_format(const uint8_t mac[6], int digits, char *out, size_t out_len)
{
    if (!mac || !out || (digits != 4 && digits != 6)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_len < (size_t)digits + 1) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (digits == 4) {
        snprintf(out, out_len, "%02X%02X", mac[4], mac[5]);
    } else {
        snprintf(out, out_len, "%02X%02X%02X", mac[3], mac[4], mac[5]);
    }
    return ESP_OK;
}

esp_err_t gw_name_join(const char *prefix, const char *id, char *out, size_t out_len)
{
    if (!prefix || !id || !out || out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int n = snprintf(out, out_len, "%s%s", prefix, id);
    return (n < 0 || (size_t)n >= out_len) ? ESP_ERR_INVALID_SIZE : ESP_OK;
}
