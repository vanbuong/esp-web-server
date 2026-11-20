#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "gw_http_common.h"
#include "gw_http_util.h"
#include "gw_setup.h"
#include "gw_store.h"

static const char *TAG = "gw_http";

gw_http_ctx_t g_http;

extern const char placeholder_start[] asm("_binary_www_placeholder_html_start");

esp_err_t gw_http_common_init(bool normal, const char *device_id, const char *hostname,
                              const char *fw_version, const gw_creds_t *defaults)
{
    g_http = (gw_http_ctx_t){
        .normal = normal, .device_id = device_id, .hostname = hostname,
        .fw_version = fw_version, .defaults = defaults,
    };
    g_http.auth = gw_auth_create(CONFIG_GW_MAX_SESSIONS, CONFIG_GW_SESSION_TIMEOUT_MIN * 60, esp_fill_random);
    return g_http.auth ? ESP_OK : ESP_ERR_NO_MEM;
}

bool gw_http_logged_in(httpd_req_t *req)
{
    char tok[GW_TOKEN_HEX_LEN + 1];
    return gw_http_session_token(req, tok) && gw_auth_check(g_http.auth, tok, gw_http_now_s());
}

esp_err_t gw_http_load_creds(gw_creds_t *c)
{
    *c = *g_http.defaults;
    return gw_store_get_password(&c->pw_set, c->salt, c->hash);
}

esp_err_t gw_http_change_password(httpd_req_t *req, const char *new_pw, char cookie_buf[160])
{
    gw_creds_t c = *g_http.defaults;
    esp_fill_random(c.salt, sizeof(c.salt));   // new random salt for every password
    ESP_RETURN_ON_ERROR(gw_pw_hash(new_pw, c.salt, sizeof(c.salt), c.iterations, c.hash), TAG, "hash");
    ESP_RETURN_ON_ERROR(gw_store_set_password(c.salt, c.hash), TAG, "store");
    // Every old session ends; the caller gets a fresh one with the new password.
    gw_auth_logout_all(g_http.auth);
    c.pw_set = true;
    char tok[GW_TOKEN_HEX_LEN + 1];
    if (gw_auth_login(g_http.auth, &c, c.user, new_pw, gw_http_now_s(), tok, NULL) == GW_LOGIN_OK) {
        gw_http_set_session_cookie(req, tok, g_http.normal, cookie_buf);
    }
    ESP_LOGI(TAG, "admin password changed");
    return ESP_OK;
}

static esp_err_t h_root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, placeholder_start);   // EMBED_TXTFILES adds a NUL terminator
}

static esp_err_t h_status(httpd_req_t *req)
{
    bool in = gw_http_logged_in(req);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "mode", g_http.normal ? "normal" : "setup");
    if (!g_http.normal) {
        cJSON_AddStringToObject(o, "step", gw_setup_step_name(gw_store_get_step()));
    }
    cJSON_AddStringToObject(o, "device_id", g_http.device_id);
    cJSON_AddStringToObject(o, "hostname", g_http.hostname);
    cJSON_AddStringToObject(o, "fw_version", g_http.fw_version);
    cJSON_AddBoolToObject(o, "logged_in", in);
    if (g_http.status_extra) {
        g_http.status_extra(req, o, in);
    }
    return gw_http_send_json(req, HTTPD_200, o);
}

static esp_err_t h_login(httpd_req_t *req)
{
    cJSON *body = gw_http_read_json(req, GW_HTTP_BODY_MAX);
    if (!body) {
        return ESP_OK;
    }
    gw_creds_t c;
    if (gw_http_load_creds(&c) != ESP_OK) {
        cJSON_Delete(body);
        return httpd_resp_send_500(req);
    }
    char tok[GW_TOKEN_HEX_LEN + 1], cookie[160];
    uint32_t retry = 0;
    gw_login_result_t r = gw_auth_login(g_http.auth, &c, gw_json_str(body, "user"),
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
    gw_http_set_session_cookie(req, tok, g_http.normal, cookie);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    cJSON_AddBoolToObject(o, "must_change_password", !c.pw_set);
    return gw_http_send_json(req, HTTPD_200, o);
}

static esp_err_t h_logout(httpd_req_t *req)
{
    char tok[GW_TOKEN_HEX_LEN + 1], cookie[160];
    if (!gw_http_session_token(req, tok) || !gw_auth_check(g_http.auth, tok, gw_http_now_s())) {
        return gw_http_send_error(req, GW_HTTP_401, "not_logged_in");
    }
    gw_auth_logout(g_http.auth, tok);
    gw_http_set_session_cookie(req, NULL, g_http.normal, cookie);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", true);
    return gw_http_send_json(req, HTTPD_200, o);
}

esp_err_t gw_http_common_register(httpd_handle_t srv)
{
    const httpd_uri_t uris[] = {
        {.uri = "/", .method = HTTP_GET, .handler = h_root},
        {.uri = "/api/status", .method = HTTP_GET, .handler = h_status},
        {.uri = "/api/login", .method = HTTP_POST, .handler = h_login},
        {.uri = "/api/logout", .method = HTTP_POST, .handler = h_logout},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(srv, &uris[i]), TAG, "uri %s", uris[i].uri);
    }
    return ESP_OK;
}
