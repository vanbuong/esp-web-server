#pragma once
// Device identity: a short ID derived from the MAC address, and names built from it
// (hostname "gateway1A2B", setup SSID "Gateway-Setup-1A2B").
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/**
 * Formats the last digits/2 bytes of mac as uppercase hex.
 * @param digits 4 or 6 (Kconfig GW_DEVICE_ID_LEN). Why the MAC: it is unique per chip and
 *               needs no provisioning; 4 digits read well, 6 make LAN collisions very unlikely.
 * @return ESP_OK, ESP_ERR_INVALID_ARG (digits not 4/6, NULL args),
 *         ESP_ERR_INVALID_SIZE (out_len < digits + 1).
 */
esp_err_t gw_dev_id_format(const uint8_t mac[6], int digits, char *out, size_t out_len);

/**
 * out = prefix + id. ESP_ERR_INVALID_SIZE if it doesn't fit (out is then not usable).
 */
esp_err_t gw_name_join(const char *prefix, const char *id, char *out, size_t out_len);
