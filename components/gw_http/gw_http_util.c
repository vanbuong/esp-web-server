#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "gw_http_util.h"

esp_err_t gw_http_send_json(httpd_req_t *req, const char *status, cJSON *obj)
{
    char *s = obj ? cJSON_PrintUnformatted(obj) : NULL;
    cJSON_Delete(obj);
    if (!s) {
        return httpd_resp_send_500(req);
    }
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    // API answers are per-session state: never let a browser or proxy cache them.
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, s);
    free(s);
    return err;
}

esp_err_t gw_http_send_error(httpd_req_t *req, const char *status, const char *code)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", code);
    return gw_http_send_json(req, status, o);
}

cJSON *gw_http_read_json(httpd_req_t *req, size_t max_len)
{
    if (req->content_len > max_len) {
        gw_http_send_error(req, GW_HTTP_413, "too_large");
        return NULL;
    }
    char ctype[48] = "";
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype));
    if (strncmp(ctype, "application/json", 16) != 0) {
        gw_http_send_error(req, GW_HTTP_415, "bad_content_type");
        return NULL;
    }
    // Heap, not stack: TLS uploads are up to 8 KB and the httpd task stack is 8 KB.
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        httpd_resp_send_500(req);
        return NULL;
    }
    size_t got = 0;
    int timeouts = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            // A phone that drops off the setup AP mid-request never closes the socket. The server
            // has a single task, so waiting forever would freeze every other client: give up.
            if (++timeouts >= GW_HTTP_RECV_TIMEOUTS) {
                httpd_resp_send_408(req);
                free(buf);
                return NULL;
            }
            continue;
        }
        if (r <= 0) {
            free(buf);
            return NULL;   // connection closed; nothing to answer
        }
        got += r;
    }
    buf[got] = 0;
    cJSON *o = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsObject(o)) {
        cJSON_Delete(o);
        gw_http_send_error(req, HTTPD_400, "bad_json");
        return NULL;
    }
    return o;
}

const char *gw_json_str(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

bool gw_http_session_token(httpd_req_t *req, char tok[GW_TOKEN_HEX_LEN + 1])
{
    // Size the buffer from the header: other sites' cookies on 192.168.4.1 can make it long,
    // and a truncated header would look like "not logged in".
    size_t len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (len == 0 || len > GW_HTTP_COOKIE_MAX) {
        return false;
    }
    char *cookie = malloc(len + 1);
    if (!cookie) {
        return false;
    }
    bool ok = httpd_req_get_hdr_value_str(req, "Cookie", cookie, len + 1) == ESP_OK &&
              gw_cookie_get(cookie, GW_COOKIE_NAME, tok, GW_TOKEN_HEX_LEN + 1);
    free(cookie);
    return ok;
}

void gw_http_set_session_cookie(httpd_req_t *req, const char *token, bool secure, char buf[160])
{
    // HttpOnly: page scripts can't read it (limits XSS damage). SameSite=Strict: other sites
    // can't make the browser send it (blocks CSRF). Secure: never sent over plain HTTP.
    snprintf(buf, 160, GW_COOKIE_NAME "=%s; Path=/; HttpOnly; SameSite=Strict%s%s", token ? token : "",
             secure ? "; Secure" : "", token ? "" : "; Max-Age=0");
    httpd_resp_set_hdr(req, "Set-Cookie", buf);
}

static void do_restart(void *arg)
{
    esp_restart();
}

void gw_http_reboot_later(uint32_t delay_ms)
{
    static esp_timer_handle_t t;
    const esp_timer_create_args_t args = {.callback = do_restart, .name = "reboot"};
    if (!t && esp_timer_create(&args, &t) != ESP_OK) {
        esp_restart();   // can't delay: reboot now
    }
    esp_timer_start_once(t, (uint64_t)delay_ms * 1000);
}

uint32_t gw_http_now_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);   // seconds since boot; no wall clock needed
}
