#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "gw_http_setup.h"
#include "gw_http_common.h"
#include "gw_http_util.h"
#include "gw_http_tls.h"
#include "gw_setup.h"
#include "gw_store.h"
#include "gw_wifi.h"

static const char *TAG = "gw_http";

// All handlers run in the single httpd task, so these need no locking.
static char s_pending_ssid[33], s_pending_pass[65];   // network under test
static bool s_pending_saved;

// Common gate for the wizard endpoints: logged in, and the step is not ahead of the device.
static bool gate(httpd_req_t *req, gw_step_t step)
{
    if (!gw_http_logged_in(req)) {
        gw_http_send_error(req, GW_HTTP_401, "not_logged_in");
        return false;
    }
    if (!gw_setup_allowed(gw_store_get_step(), step)) {
        gw_http_send_error(req, GW_HTTP_409, "wrong_step");
        return false;
    }
    return true;
}

static esp_err_t complete_step(gw_step_t done)
{
    gw_step_t cur = gw_store_get_step();
    gw_step_t next = gw_setup_complete(cur, done);
    return next == cur ? ESP_OK : gw_store_set_step(next);
}

static esp_err_t h_password(httpd_req_t *req)
{
    if (!gate(req, GW_STEP_PASSWORD)) {
        return ESP_OK;
    }
    cJSON *body = gw_http_read_json(req, GW_HTTP_BODY_MAX);
    if (!body) {
        return ESP_OK;
    }
    const char *pw = gw_json_str(body, "new_password");
    if (!gw_pw_policy_ok(pw)) {
        cJSON_Delete(body);
        return gw_http_send_error(req, HTTPD_400, "weak_password");
    }
    char cookie[160];
    esp_err_t err = gw_http_change_password(req, pw, cookie);   // data first ...
    if (err == ESP_OK) {
        err = complete_step(GW_STEP_PASSWORD);                     // ... then the step
    }
    cJSON_Delete(body);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "password save: %s", esp_err_to_name(err));
        return httpd_resp_send_500(req);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return gw_http_send_json(req, HTTPD_200, o);
}

static esp_err_t h_scan(httpd_req_t *req)
{
    if (!gate(req, GW_STEP_WIFI)) {
        return ESP_OK;
    }
    gw_ap_info_t list[GW_SCAN_MAX];
    size_t n = 0;
    esp_err_t err = gw_wifi_scan(list, GW_SCAN_MAX, &n);
    if (err == ESP_ERR_INVALID_STATE) {
        return gw_http_send_error(req, GW_HTTP_409, "test_running");
    }
    if (err != ESP_OK) {
        return gw_http_send_error(req, GW_HTTP_503, "scan_failed");
    }
    char *json = gw_ap_list_json(list, n);
    if (!json) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    err = httpd_resp_sendstr(req, json);
    free(json);
    return err;
}

static esp_err_t h_wifi_test(httpd_req_t *req)
{
    if (!gate(req, GW_STEP_WIFI)) {
        return ESP_OK;
    }
    cJSON *body = gw_http_read_json(req, GW_HTTP_BODY_MAX);
    if (!body) {
        return ESP_OK;
    }
    const char *ssid = gw_json_str(body, "ssid");
    const char *pass = gw_json_str(body, "password");
    if (!pass) {
        pass = "";   // open network
    }
    if (!gw_wifi_creds_valid(ssid, pass)) {
        cJSON_Delete(body);
        return gw_http_send_error(req, HTTPD_400, "invalid_credentials");
    }
    if (gw_wifi_test_get(NULL, 0) == GW_WIFI_TEST_RUNNING) {
        cJSON_Delete(body);
        return gw_http_send_error(req, GW_HTTP_409, "test_running");
    }
    strlcpy(s_pending_ssid, ssid, sizeof(s_pending_ssid));
    strlcpy(s_pending_pass, pass, sizeof(s_pending_pass));
    s_pending_saved = false;
    cJSON_Delete(body);
    if (gw_wifi_test_start(s_pending_ssid, s_pending_pass) == ESP_ERR_INVALID_STATE) {
        return gw_http_send_error(req, GW_HTTP_409, "test_running");
    }
    ESP_LOGI(TAG, "testing Wi-Fi \"%s\"", s_pending_ssid);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", "running");
    return gw_http_send_json(req, GW_HTTP_202, o);
}

static esp_err_t h_wifi_result(httpd_req_t *req)
{
    if (!gate(req, GW_STEP_WIFI)) {
        return ESP_OK;
    }
    char reason[16];
    gw_wifi_test_state_t st = gw_wifi_test_get(reason, sizeof(reason));
    if (st == GW_WIFI_TEST_OK && !s_pending_saved) {
        // Saved only after it worked, and before the step advances.
        esp_err_t err = gw_store_set_wifi(s_pending_ssid, s_pending_pass);
        if (err == ESP_OK) {
            err = complete_step(GW_STEP_WIFI);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "wifi save: %s", esp_err_to_name(err));
            return httpd_resp_send_500(req);
        }
        s_pending_saved = true;
        ESP_LOGI(TAG, "Wi-Fi \"%s\" saved", s_pending_ssid);
    }
    static const char *const names[] = {"idle", "running", "ok", "failed"};
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "state", names[st]);
    if (st == GW_WIFI_TEST_FAILED) {
        cJSON_AddStringToObject(o, "reason", reason);
    }
    return gw_http_send_json(req, HTTPD_200, o);
}

static esp_err_t h_tls_csr(httpd_req_t *req)
{
    if (!gate(req, GW_STEP_TLS)) {
        return ESP_OK;
    }
    char *csr = NULL;
    if (gw_http_tls_csr(g_http.hostname, false, &csr) != ESP_OK) {
        return httpd_resp_send_500(req);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "csr", csr);
    free(csr);
    return gw_http_send_json(req, HTTPD_200, o);
}

// CSR path: {"cert","ca"} (uses the pending key). Upload path: {"key","cert","ca"}.
static esp_err_t h_tls_cert(httpd_req_t *req)
{
    if (!gate(req, GW_STEP_TLS)) {
        return ESP_OK;
    }
    cJSON *body = gw_http_read_json(req, GW_HTTP_TLS_BODY_MAX);
    if (!body) {
        return ESP_OK;
    }
    char *key = (char *)gw_json_str(body, "key");    // cJSON strings are heap copies: may be edited
    char *cert = (char *)gw_json_str(body, "cert");
    char *ca = (char *)gw_json_str(body, "ca");
    gw_tls_info_t info;
    const char *err = gw_http_tls_accept(key, cert ? cert : (char[]){""}, ca ? ca : (char[]){""},
                                         g_http.hostname, &info);
    cJSON_Delete(body);
    if (err) {
        return gw_http_send_error(req, HTTPD_400, err);
    }
    if (complete_step(GW_STEP_TLS) != ESP_OK) {
        return httpd_resp_send_500(req);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddStringToObject(o, "subject", info.subject_cn);
    cJSON_AddStringToObject(o, "issuer", info.issuer_cn);
    cJSON_AddStringToObject(o, "not_after", info.not_after);
    return gw_http_send_json(req, HTTPD_200, o);
}

static esp_err_t h_finish(httpd_req_t *req)
{
    if (!gate(req, GW_STEP_FINISH)) {
        return ESP_OK;
    }
    gw_tls_rec_t tls;
    char ssid[33], pass[65];
    if (gw_store_get_tls(&tls) != ESP_OK) {
        return gw_http_send_error(req, GW_HTTP_409, "tls_missing");
    }
    gw_store_tls_free(&tls);
    if (gw_store_get_wifi(ssid, pass) != ESP_OK) {
        return gw_http_send_error(req, GW_HTTP_409, "wifi_missing");
    }
    memset(pass, 0, sizeof(pass));
    // State first, then step: a power cut in between still boots into normal mode.
    if (gw_store_set_state(GW_STATE_PROVISIONED) != ESP_OK || gw_store_set_step(GW_STEP_DONE) != ESP_OK) {
        return httpd_resp_send_500(req);
    }
    ESP_LOGI(TAG, "setup finished — rebooting into normal mode");
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddBoolToObject(o, "rebooting", true);
    esp_err_t r = gw_http_send_json(req, HTTPD_200, o);
    gw_http_reboot_later(2000);
    return r;
}

// Captive portal: anything unknown (incl. the OS connectivity checks such as /generate_204 or
// /hotspot-detect.html) is redirected to the setup page, which makes phones show it.
static esp_err_t on_404(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t gw_http_setup_start(const gw_http_setup_cfg_t *cfg)
{
    ESP_RETURN_ON_ERROR(gw_http_common_init(false, cfg->device_id, cfg->hostname, cfg->fw_version,
                                            cfg->defaults), TAG, "init");
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.max_uri_handlers = 12;
    hc.max_open_sockets = 7;    // browsers open several connections; oldest idle one is closed
    hc.lru_purge_enable = true;
    hc.stack_size = 8192;       // PBKDF2 + cJSON in handlers
    httpd_handle_t srv;
    ESP_RETURN_ON_ERROR(httpd_start(&srv, &hc), TAG, "start");
    ESP_RETURN_ON_ERROR(gw_http_common_register(srv), TAG, "common");

    const httpd_uri_t uris[] = {
        {.uri = "/api/setup/password", .method = HTTP_POST, .handler = h_password},
        {.uri = "/api/setup/wifi/scan", .method = HTTP_GET, .handler = h_scan},
        {.uri = "/api/setup/wifi/test", .method = HTTP_POST, .handler = h_wifi_test},
        {.uri = "/api/setup/wifi/result", .method = HTTP_GET, .handler = h_wifi_result},
        {.uri = "/api/setup/tls/csr", .method = HTTP_GET, .handler = h_tls_csr},
        {.uri = "/api/setup/tls/cert", .method = HTTP_POST, .handler = h_tls_cert},
        {.uri = "/api/setup/finish", .method = HTTP_POST, .handler = h_finish},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(srv, &uris[i]), TAG, "uri %s", uris[i].uri);
    }
    ESP_RETURN_ON_ERROR(httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, on_404), TAG, "404");
    ESP_LOGI(TAG, "setup server on http://%s.local/ and http://192.168.4.1/", cfg->hostname);
    return ESP_OK;
}
