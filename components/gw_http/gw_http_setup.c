#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "gw_http_setup.h"
#include "gw_http_util.h"
#include "gw_setup.h"
#include "gw_store.h"
#include "gw_wifi.h"

static const char *TAG = "gw_http";

// All handlers run in the single httpd task, so these need no locking.
static gw_http_setup_cfg_t s_cfg;
static gw_auth_t *s_auth;
static char s_pending_ssid[33], s_pending_pass[65];   // network under test
static bool s_pending_saved;

extern const char placeholder_start[] asm("_binary_www_placeholder_html_start");

static bool logged_in(httpd_req_t *req)
{
    char tok[GW_TOKEN_HEX_LEN + 1];
    return gw_http_session_token(req, tok) && gw_auth_check(s_auth, tok, gw_http_now_s());
}

// Current credentials: Kconfig defaults + whatever password is stored.
static esp_err_t load_creds(gw_creds_t *c)
{
    *c = *s_cfg.defaults;
    return gw_store_get_password(&c->pw_set, c->salt, c->hash);
}

// Common gate for the wizard endpoints: logged in, and the step is not ahead of the device.
static bool gate(httpd_req_t *req, gw_step_t step)
{
    if (!logged_in(req)) {
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

static esp_err_t h_status(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mode", "setup");
    cJSON_AddStringToObject(o, "step", gw_setup_step_name(gw_store_get_step()));
    cJSON_AddStringToObject(o, "device_id", s_cfg.device_id);
    cJSON_AddStringToObject(o, "hostname", s_cfg.hostname);
    cJSON_AddStringToObject(o, "fw_version", s_cfg.fw_version);
    cJSON_AddBoolToObject(o, "logged_in", logged_in(req));
    return gw_http_send_json(req, HTTPD_200, o);
}

static esp_err_t h_login(httpd_req_t *req)
{
    cJSON *body = gw_http_read_json(req);
    if (!body) {
        return ESP_OK;
    }
    gw_creds_t c;
    if (load_creds(&c) != ESP_OK) {
        cJSON_Delete(body);
        return httpd_resp_send_500(req);
    }
    char tok[GW_TOKEN_HEX_LEN + 1], cookie[128];
    uint32_t retry = 0;
    gw_login_result_t r = gw_auth_login(s_auth, &c, gw_json_str(body, "user"),
                                        gw_json_str(body, "password"), gw_http_now_s(), tok, &retry);
    cJSON_Delete(body);
    if (r == GW_LOGIN_LOCKED) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "error", "locked");
        cJSON_AddNumberToObject(o, "retry_after", retry);
        return gw_http_send_json(req, GW_HTTP_429, o);
    }
    if (r != GW_LOGIN_OK) {
        ESP_LOGW(TAG, "login failed");
        return gw_http_send_error(req, GW_HTTP_401, "bad_credentials");
    }
    gw_http_set_session_cookie(req, tok, cookie);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddBoolToObject(o, "must_change_password", !c.pw_set);
    return gw_http_send_json(req, HTTPD_200, o);
}

static esp_err_t h_logout(httpd_req_t *req)
{
    char tok[GW_TOKEN_HEX_LEN + 1], cookie[128];
    if (!gw_http_session_token(req, tok) || !gw_auth_check(s_auth, tok, gw_http_now_s())) {
        return gw_http_send_error(req, GW_HTTP_401, "not_logged_in");
    }
    gw_auth_logout(s_auth, tok);
    gw_http_set_session_cookie(req, NULL, cookie);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return gw_http_send_json(req, HTTPD_200, o);
}

static esp_err_t h_password(httpd_req_t *req)
{
    if (!gate(req, GW_STEP_PASSWORD)) {
        return ESP_OK;
    }
    cJSON *body = gw_http_read_json(req);
    if (!body) {
        return ESP_OK;
    }
    const char *pw = gw_json_str(body, "new_password");
    if (!gw_pw_policy_ok(pw)) {
        cJSON_Delete(body);
        return gw_http_send_error(req, HTTPD_400, "weak_password");
    }
    gw_creds_t c = *s_cfg.defaults;
    esp_fill_random(c.salt, sizeof(c.salt));   // new random salt for every password
    esp_err_t err = gw_pw_hash(pw, c.salt, sizeof(c.salt), c.iterations, c.hash);
    if (err == ESP_OK) {
        err = gw_store_set_password(c.salt, c.hash);   // data first ...
    }
    if (err == ESP_OK) {
        err = complete_step(GW_STEP_PASSWORD);          // ... then the step (see gw_store.h)
    }
    if (err != ESP_OK) {
        cJSON_Delete(body);
        ESP_LOGE(TAG, "password save: %s", esp_err_to_name(err));
        return httpd_resp_send_500(req);
    }
    // Every old session ends; the caller gets a fresh one with the new password.
    gw_auth_logout_all(s_auth);
    c.pw_set = true;
    char tok[GW_TOKEN_HEX_LEN + 1], cookie[128];
    gw_login_result_t r = gw_auth_login(s_auth, &c, c.user, pw, gw_http_now_s(), tok, NULL);
    cJSON_Delete(body);
    if (r == GW_LOGIN_OK) {
        gw_http_set_session_cookie(req, tok, cookie);
    }
    ESP_LOGI(TAG, "admin password changed");
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
    cJSON *body = gw_http_read_json(req);
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

static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, placeholder_start);   // EMBED_TXTFILES adds a NUL terminator
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
    s_cfg = *cfg;
    s_auth = gw_auth_create(CONFIG_GW_MAX_SESSIONS, CONFIG_GW_SESSION_TIMEOUT_MIN * 60, esp_fill_random);
    ESP_RETURN_ON_FALSE(s_auth, ESP_ERR_NO_MEM, TAG, "auth");

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.max_uri_handlers = 12;
    hc.max_open_sockets = 7;    // browsers open several connections; oldest idle one is closed
    hc.lru_purge_enable = true;
    hc.stack_size = 8192;       // PBKDF2 + cJSON in handlers
    httpd_handle_t srv;
    ESP_RETURN_ON_ERROR(httpd_start(&srv, &hc), TAG, "start");

    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = h_root},
        {.uri = "/api/status", .method = HTTP_GET, .handler = h_status},
        {.uri = "/api/login", .method = HTTP_POST, .handler = h_login},
        {.uri = "/api/logout", .method = HTTP_POST, .handler = h_logout},
        {.uri = "/api/setup/password", .method = HTTP_POST, .handler = h_password},
        {.uri = "/api/setup/wifi/scan", .method = HTTP_GET, .handler = h_scan},
        {.uri = "/api/setup/wifi/test", .method = HTTP_POST, .handler = h_wifi_test},
        {.uri = "/api/setup/wifi/result", .method = HTTP_GET, .handler = h_wifi_result},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(srv, &uris[i]), TAG, "uri %s", uris[i].uri);
    }
    ESP_RETURN_ON_ERROR(httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, on_404), TAG, "404");
    ESP_LOGI(TAG, "setup server on http://%s.local/ and http://192.168.4.1/", s_cfg.hostname);
    return ESP_OK;
}
