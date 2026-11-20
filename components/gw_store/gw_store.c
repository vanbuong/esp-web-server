#include <string.h>
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "gw_store.h"

static const char *TAG = "gw_store";

#define NS_MAIN "gw"            // in the default "nvs" partition (erased by factory reset)
#define PART_FACTORY "factory"  // survives factory reset
#define NS_FACTORY "fct"

static esp_err_t init_part(const char *label)
{
    esp_err_t err = nvs_flash_init_partition(label);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // Partition full or written by a newer NVS format: unusable as is, start it fresh.
        ESP_LOGW(TAG, "erasing partition %s (%s)", label, esp_err_to_name(err));
        ESP_RETURN_ON_ERROR(nvs_flash_erase_partition(label), TAG, "erase %s", label);
        err = nvs_flash_init_partition(label);
    }
    return err;
}

esp_err_t gw_store_init(void)
{
    ESP_RETURN_ON_ERROR(init_part(NVS_DEFAULT_PART_NAME), TAG, "init nvs");
    ESP_RETURN_ON_ERROR(init_part(PART_FACTORY), TAG, "init factory");
    return ESP_OK;
}

// Small helpers: open, do one thing, commit, close. Setters commit before returning, so the
// value is in flash when the caller moves on.
static esp_err_t get_u8(const char *key, uint8_t *v)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_MAIN, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_u8(h, key, v);
        nvs_close(h);
    }
    return err;   // ESP_ERR_NVS_NOT_FOUND when namespace or key is missing
}

static esp_err_t set_u8(const char *key, uint8_t v)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS_MAIN, NVS_READWRITE, &h), TAG, "open");
    esp_err_t err = nvs_set_u8(h, key, v);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

gw_state_t gw_store_get_state(void)
{
    uint8_t v;
    return (get_u8("state", &v) == ESP_OK && v == GW_STATE_PROVISIONED) ? GW_STATE_PROVISIONED
                                                                       : GW_STATE_SETUP;
}

esp_err_t gw_store_set_state(gw_state_t s)
{
    return set_u8("state", (uint8_t)s);
}

gw_step_t gw_store_get_step(void)
{
    uint8_t v;
    return get_u8("setup_step", &v) == ESP_OK ? gw_setup_sanitize(v) : GW_STEP_PASSWORD;
}

esp_err_t gw_store_set_step(gw_step_t s)
{
    return set_u8("setup_step", (uint8_t)s);
}

static esp_err_t get_blob(nvs_handle_t h, const char *key, void *out, size_t len)
{
    size_t n = len;
    esp_err_t err = nvs_get_blob(h, key, out, &n);
    return (err == ESP_OK && n != len) ? ESP_ERR_INVALID_SIZE : err;
}

esp_err_t gw_store_get_password(bool *is_set, uint8_t salt[GW_PW_SALT_LEN], uint8_t hash[GW_PW_HASH_LEN])
{
    *is_set = false;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_MAIN, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;   // nothing saved yet
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open");
    err = get_blob(h, "pw_salt", salt, GW_PW_SALT_LEN);
    if (err == ESP_OK) {
        err = get_blob(h, "pw_hash", hash, GW_PW_HASH_LEN);
    }
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    *is_set = (err == ESP_OK);
    return err;
}

esp_err_t gw_store_set_password(const uint8_t salt[GW_PW_SALT_LEN], const uint8_t hash[GW_PW_HASH_LEN])
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS_MAIN, NVS_READWRITE, &h), TAG, "open");
    esp_err_t err = nvs_set_blob(h, "pw_salt", salt, GW_PW_SALT_LEN);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, "pw_hash", hash, GW_PW_HASH_LEN);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static esp_err_t get_str(nvs_handle_t h, const char *key, char *out, size_t len)
{
    size_t n = len;
    return nvs_get_str(h, key, out, &n);
}

esp_err_t gw_store_get_wifi(char ssid[33], char pass[65])
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS_MAIN, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = get_str(h, "wifi_ssid", ssid, 33);
        if (err == ESP_OK) {
            err = get_str(h, "wifi_pass", pass, 65);
        }
        nvs_close(h);
    }
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
}

esp_err_t gw_store_set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS_MAIN, NVS_READWRITE, &h), TAG, "open");
    esp_err_t err = nvs_set_str(h, "wifi_ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "wifi_pass", pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t gw_store_get_ap_password(char out[GW_AP_PW_LEN + 1])
{
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(PART_FACTORY, NS_FACTORY, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = get_str(h, "ap_pass", out, GW_AP_PW_LEN + 1);
        nvs_close(h);
    }
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
}

esp_err_t gw_store_set_ap_password(const char *pw)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open_from_partition(PART_FACTORY, NS_FACTORY, NVS_READWRITE, &h),
                        TAG, "open factory");
    esp_err_t err = nvs_set_str(h, "ap_pass", pw);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t gw_store_factory_reset(void)
{
    return nvs_flash_erase();   // default "nvs" partition only; "factory" is untouched
}
