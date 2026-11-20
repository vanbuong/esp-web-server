#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_https_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "mbedtls/oid.h"
#include "mbedtls/ssl.h"
#include "gw_http_admin.h"
#include "gw_http_common.h"
#include "gw_http_util.h"
#include "gw_time.h"
#include "gw_wifi.h"

static const char *TAG = "gw_https";

#if CONFIG_GW_TLS_MODE_MUTUAL
#define TLS_MUTUAL 1
#else
#define TLS_MUTUAL 0   // the Kconfig symbol is undefined in server-auth builds
#endif

static gw_tls_info_t s_cert;   // shown in /api/status

// Client-certificate CN per open TLS socket (mutual mode). Written by the TLS session callback,
// read by handlers; both run in the httpd task, but a lock costs nothing here.
#define MAX_TLS_SESS 8
static struct { int fd; char cn[64]; } s_sess[MAX_TLS_SESS];
static portMUX_TYPE s_sess_lock = portMUX_INITIALIZER_UNLOCKED;

static void peer_cn(esp_tls_t *tls, char *out, size_t len)
{
    out[0] = 0;
    mbedtls_ssl_context *ssl = esp_tls_get_ssl_context(tls);
    const mbedtls_x509_crt *peer = ssl ? mbedtls_ssl_get_peer_cert(ssl) : NULL;
    for (const mbedtls_x509_name *n = peer ? &peer->subject : NULL; n; n = n->next) {
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid) == 0) {
            size_t l = n->val.len < len - 1 ? n->val.len : len - 1;
            memcpy(out, n->val.p, l);
            out[l] = 0;
            return;
        }
    }
}

static void on_tls_session(esp_https_server_user_cb_arg_t *arg)
{
    int fd = -1;
    if (esp_tls_get_conn_sockfd(arg->tls, &fd) != ESP_OK) {
        return;
    }
    char cn[64] = "";
    if (arg->user_cb_state == HTTPD_SSL_USER_CB_SESS_CREATE) {
        peer_cn(arg->tls, cn, sizeof(cn));
        if (cn[0]) {
            ESP_LOGI(TAG, "client certificate CN=%s (socket %d)", cn, fd);
        }
    }
    portENTER_CRITICAL(&s_sess_lock);
    for (int i = 0; i < MAX_TLS_SESS; i++) {
        bool create = arg->user_cb_state == HTTPD_SSL_USER_CB_SESS_CREATE;
        if (create ? s_sess[i].fd <= 0 : s_sess[i].fd == fd) {
            s_sess[i].fd = create ? fd : 0;
            strlcpy(s_sess[i].cn, cn, sizeof(s_sess[i].cn));
            break;
        }
    }
    portEXIT_CRITICAL(&s_sess_lock);
}

static void client_cn(httpd_req_t *req, char *out, size_t len)
{
    int fd = httpd_req_to_sockfd(req);
    out[0] = 0;
    portENTER_CRITICAL(&s_sess_lock);
    for (int i = 0; i < MAX_TLS_SESS; i++) {
        if (s_sess[i].fd == fd) {
            strlcpy(out, s_sess[i].cn, len);
            break;
        }
    }
    portEXIT_CRITICAL(&s_sess_lock);
}

static void status_extra(httpd_req_t *req, cJSON *o, bool logged_in)
{
    cJSON_AddStringToObject(o, "tls_mode", TLS_MUTUAL ? "mutual" : "server_auth");
    char cn[64];
    client_cn(req, cn, sizeof(cn));
    if (cn[0]) {
        cJSON_AddStringToObject(o, "client_cn", cn);
    } else {
        cJSON_AddNullToObject(o, "client_cn");
    }
    if (!logged_in) {
        return;   // device details only for the admin
    }
    char ip[16];
    gw_wifi_station_ip(ip, sizeof(ip));
    cJSON_AddStringToObject(o, "ip", ip);
    cJSON_AddNumberToObject(o, "rssi", gw_wifi_station_rssi());
    cJSON_AddNumberToObject(o, "uptime_s", gw_http_now_s());
    cJSON_AddNumberToObject(o, "heap_free", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(o, "heap_min", heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    cJSON_AddBoolToObject(o, "time_valid", gw_time_valid());
    cJSON *c = cJSON_AddObjectToObject(o, "cert");
    cJSON_AddStringToObject(c, "subject", s_cert.subject_cn);
    cJSON_AddStringToObject(c, "issuer", s_cert.issuer_cn);
    cJSON_AddStringToObject(c, "not_after", s_cert.not_after);
    cJSON_AddStringToObject(c, "key_type", s_cert.key_type);
}

esp_err_t gw_http_admin_register(httpd_handle_t srv);   // gw_http_admin_api.c (task 9)

esp_err_t gw_http_admin_start(const gw_http_admin_cfg_t *cfg)
{
    // Re-check the stored identity: catches NVS damage before browsers see a broken handshake.
    const char *bad = gw_tls_check(cfg->key_pem, cfg->cert_pem, cfg->ca_pem, cfg->hostname, &s_cert);
    if (bad) {
        ESP_LOGE(TAG, "stored TLS identity invalid: %s", bad);
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(gw_http_common_init(true, cfg->device_id, cfg->hostname, cfg->fw_version,
                                            cfg->defaults), TAG, "init");
    g_http.status_extra = status_extra;

    httpd_ssl_config_t c = HTTPD_SSL_CONFIG_DEFAULT();   // port 443, ctrl port 32769
    c.servercert = (const uint8_t *)cfg->cert_pem;
    c.servercert_len = strlen(cfg->cert_pem) + 1;
    c.prvtkey_pem = (const uint8_t *)cfg->key_pem;
    c.prvtkey_len = strlen(cfg->key_pem) + 1;
#if TLS_MUTUAL
    // Giving esp_https_server a CA turns on client verification (MBEDTLS_SSL_VERIFY_REQUIRED).
    c.cacert_pem = (const uint8_t *)cfg->ca_pem;
    c.cacert_len = strlen(cfg->ca_pem) + 1;
#endif
    c.user_cb = on_tls_session;
    // Each TLS session costs ~30-40 KB of RAM on a classic ESP32: keep few, recycle idle ones.
    c.httpd.max_open_sockets = 4;
    c.httpd.lru_purge_enable = true;
    c.httpd.max_uri_handlers = 16;
    httpd_handle_t srv;
    ESP_RETURN_ON_ERROR(httpd_ssl_start(&srv, &c), TAG, "https start");
    ESP_RETURN_ON_ERROR(gw_http_common_register(srv), TAG, "common");
    ESP_RETURN_ON_ERROR(gw_http_admin_register(srv), TAG, "admin");
    ESP_LOGI(TAG, "HTTPS on https://%s.local/ (%s TLS), certificate valid until %s", cfg->hostname,
             TLS_MUTUAL ? "mutual" : "server-auth", s_cert.not_after);
    return ESP_OK;
}

// --- port 80: redirect only -------------------------------------------------------------------

static char s_redirect_host[48];

static esp_err_t redirect(httpd_req_t *req)
{
    char loc[48 + 8 + 16 + 128];
    snprintf(loc, sizeof(loc), "https://%s.local%.128s", s_redirect_host, req->uri);
    httpd_resp_set_status(req, "301 Moved Permanently");
    httpd_resp_set_hdr(req, "Location", loc);
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t redirect_404(httpd_req_t *req, httpd_err_code_t err)
{
    return redirect(req);   // every path, every method
}

esp_err_t gw_http_redirect_start(const char *hostname)
{
    strlcpy(s_redirect_host, hostname, sizeof(s_redirect_host));
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();   // port 80, ctrl port 32768
    hc.max_open_sockets = 2;
    hc.lru_purge_enable = true;
    hc.stack_size = 3072;
    httpd_handle_t srv;
    ESP_RETURN_ON_ERROR(httpd_start(&srv, &hc), TAG, "http start");
    return httpd_register_err_handler(srv, HTTPD_404_NOT_FOUND, redirect_404);
}
