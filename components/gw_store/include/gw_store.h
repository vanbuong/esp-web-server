#pragma once
// Persistent device state in NVS (ESP-IDF's key-value store in flash).
// Two partitions: "nvs" holds everything a factory reset erases; "factory" holds per-device
// data that must survive it (the setup Wi-Fi password printed on the label).
// Each credential set (password salt+hash, Wi-Fi ssid+password) is one NVS key, so it is
// replaced atomically. Rule for callers: save a step's data BEFORE advancing the step, so a power cut can only
// make the user repeat a step, never skip one.
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "gw_setup.h"
#include "gw_password.h"
#include "gw_wifi_util.h"

typedef enum { GW_STATE_SETUP = 0, GW_STATE_PROVISIONED = 1 } gw_state_t;

/** Initializes both partitions; erases and retries a partition NVS can't use (full / newer format). */
esp_err_t gw_store_init(void);

gw_state_t gw_store_get_state(void);          // missing → SETUP
esp_err_t gw_store_set_state(gw_state_t s);

gw_step_t gw_store_get_step(void);            // missing or invalid → PASSWORD
esp_err_t gw_store_set_step(gw_step_t s);

/** *is_set = false (ESP_OK) while no password was saved yet. */
esp_err_t gw_store_get_password(bool *is_set, uint8_t salt[GW_PW_SALT_LEN], uint8_t hash[GW_PW_HASH_LEN]);
esp_err_t gw_store_set_password(const uint8_t salt[GW_PW_SALT_LEN], const uint8_t hash[GW_PW_HASH_LEN]);

/** ESP_ERR_NOT_FOUND if none saved. */
esp_err_t gw_store_get_wifi(char ssid[33], char pass[65]);
esp_err_t gw_store_set_wifi(const char *ssid, const char *pass);

/** "factory" partition. ESP_ERR_NOT_FOUND if none saved. */
esp_err_t gw_store_get_ap_password(char out[GW_AP_PW_LEN + 1]);
esp_err_t gw_store_set_ap_password(const char *pw);

/** Erases the "nvs" partition only (state, password, Wi-Fi). Call gw_store_init() again after. */
esp_err_t gw_store_factory_reset(void);
