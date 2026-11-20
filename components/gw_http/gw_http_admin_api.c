// Admin endpoints on the HTTPS server (normal mode). Every one requires a logged-in session.
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "gw_http_common.h"
#include "gw_http_tls.h"
#include "gw_http_util.h"
#include "gw_store.h"
#include "gw_tls_csr.h"

static const char *TAG = "gw_admin";

const gw_tls_info_t *gw_http_admin_cert(void);   // gw_http_admin.c

static bool need_login(httpd_req_t *req)
{
    if (gw_http_logged_in(req)) {
        return true;
    }
    gw_http_send_error(req, GW_HTTP_401, "not_logged_in");
    return false;
}

// Re-entering the password for destructive actions: a stolen session cookie alone isn't enough.
static bool password_ok(const char *pw)
{
    gw_creds_t c;
    return pw && gw_http_load_creds(&c) == ESP_OK && c.pw_set &&
           gw_pw_verify(pw, c.salt, c.hash, c.iterations);
}

static esp_err_t send_ok(httpd_req_t *req, cJSON *o)
{
    if (!o) {
        o = cJSON_CreateObject();
    }
    cJSON_AddBoolToObject(o, "ok", true);
    return gw_http_send_json(req, HTTPD_200, o);
}

static esp_err_t h_password(httpd_req_t *req)
{
    if (!need_login(req)) {
        return ESP_OK;
    }
    cJSON *body = gw_http_read_json(req, GW_HTTP_BODY_MAX);
    if (!body) {
        return ESP_OK;
    }
    const char *cur = gw_json_str(body, "current_password");
    const char *pw = gw_json_str(body, "new_password");
    esp_err_t r;
    char cookie[160];
    if (!password_ok(cur)) {
        r = gw_http_send_error(req, GW_HTTP_403, "wrong_password");
    } else if (!gw_pw_policy_ok(pw)) {
        r = gw_http_send_error(req, HTTPD_400, "weak_password");
    } else if (gw_http_change_password(req, pw, cookie) != ESP_OK) {
        r = httpd_resp_send_500(req);
    } else {
        r = send_ok(req, NULL);
    }
    cJSON_Delete(body);
    return r;
}

static esp_err_t h_tls_get(httpd_req_t *req)
{
    if (!need_login(req)) {
        return ESP_OK;
    }
    const gw_tls_info_t *c = gw_http_admin_cert();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "subject", c->subject_cn);
    cJSON_AddStringToObject(o, "issuer", c->issuer_cn);
    cJSON_AddStringToObject(o, "not_after", c->not_after);
    cJSON_AddStringToObject(o, "key_type", c->key_type);
    // A pending key = a CSR was made and is waiting for its certificate. Only its presence is
    // reported; the key is wiped from RAM right away and never leaves the device.
    char key[GW_KEY_PEM_MAX];
    cJSON_AddBoolToObject(o, "pending_csr", gw_store_get_pending_key(key, sizeof(key)) == ESP_OK);
    memset(key, 0, sizeof(key));
    return gw_http_send_json(req, HTTPD_200, o);
}

// Renewal step 1: a new key + CSR. The running identity keeps being served until a matching
// certificate is uploaded and the device reboots.
static esp_err_t h_tls_csr(httpd_req_t *req)
{
    if (!need_login(req)) {
        return ESP_OK;
    }
    char *csr = NULL;
    if (gw_http_tls_csr(g_http.hostname, true, &csr) != ESP_OK) {
        return httpd_resp_send_500(req);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "csr", csr);
    free(csr);
    return gw_http_send_json(req, HTTPD_200, o);
}

// Renewal step 2 (or a fresh upload). Stored for the next boot; the live server keeps its copy.
static esp_err_t h_tls_cert(httpd_req_t *req)
{
    if (!need_login(req)) {
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
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "reboot_required", true);
    cJSON_AddStringToObject(o, "subject", info.subject_cn);
    cJSON_AddStringToObject(o, "not_after", info.not_after);
    return send_ok(req, o);
}

static esp_err_t h_reboot(httpd_req_t *req)
{
    if (!need_login(req)) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "reboot requested");
    esp_err_t r = send_ok(req, NULL);
    gw_http_reboot_later(2000);
    return r;
}

static esp_err_t h_factory_reset(httpd_req_t *req)
{
    if (!need_login(req)) {
        return ESP_OK;
    }
    cJSON *body = gw_http_read_json(req, GW_HTTP_BODY_MAX);
    if (!body) {
        return ESP_OK;
    }
    bool ok = password_ok(gw_json_str(body, "password"));
    cJSON_Delete(body);
    if (!ok) {
        return gw_http_send_error(req, GW_HTTP_403, "wrong_password");
    }
    ESP_LOGW(TAG, "factory reset requested — erasing settings");
    if (gw_store_factory_reset() != ESP_OK) {
        return httpd_resp_send_500(req);
    }
    esp_err_t r = send_ok(req, NULL);
    gw_http_reboot_later(2000);
    return r;
}

esp_err_t gw_http_admin_register(httpd_handle_t srv)
{
    const httpd_uri_t uris[] = {
        {.uri = "/api/admin/password", .method = HTTP_POST, .handler = h_password},
        {.uri = "/api/admin/tls", .method = HTTP_GET, .handler = h_tls_get},
        {.uri = "/api/admin/tls/csr", .method = HTTP_POST, .handler = h_tls_csr},
        {.uri = "/api/admin/tls/cert", .method = HTTP_POST, .handler = h_tls_cert},
        {.uri = "/api/admin/reboot", .method = HTTP_POST, .handler = h_reboot},
        {.uri = "/api/admin/factory-reset", .method = HTTP_POST, .handler = h_factory_reset},
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(srv, &uris[i]), TAG, "uri %s", uris[i].uri);
    }
    return ESP_OK;
}
