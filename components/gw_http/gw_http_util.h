#pragma once
// Private helpers shared by the HTTP handlers: JSON in/out, limits, session cookie.
#include <stdbool.h>
#include <stdint.h>
#include "esp_http_server.h"
#include "cJSON.h"
#include "gw_auth.h"

#define GW_HTTP_BODY_MAX 512
#define GW_HTTP_RECV_TIMEOUTS 3     // x 5 s socket timeout before a stalled body is abandoned
#define GW_HTTP_COOKIE_MAX 4096     // browsers cap a Cookie header around 4 KB
#define GW_COOKIE_NAME "gw_session"

// esp_http_server only defines a few status strings; the rest are spelled out here.
#define GW_HTTP_202 "202 Accepted"
#define GW_HTTP_401 "401 Unauthorized"
#define GW_HTTP_409 "409 Conflict"
#define GW_HTTP_413 "413 Payload Too Large"
#define GW_HTTP_415 "415 Unsupported Media Type"
#define GW_HTTP_429 "429 Too Many Requests"
#define GW_HTTP_503 "503 Service Unavailable"

/** Sends obj as JSON with the given status (e.g. HTTPD_200) and frees it. */
esp_err_t gw_http_send_json(httpd_req_t *req, const char *status, cJSON *obj);

/** Sends {"error":"<code>"}. */
esp_err_t gw_http_send_error(httpd_req_t *req, const char *status, const char *code);

/**
 * Reads and parses a JSON body of at most max_len bytes (GW_HTTP_BODY_MAX for most endpoints). On a problem it already sent the error response
 * (413 too_large, 415 bad_content_type, 400 bad_json) and returns NULL.
 */
cJSON *gw_http_read_json(httpd_req_t *req, size_t max_len);

/** String field or NULL if missing / not a string. */
const char *gw_json_str(const cJSON *obj, const char *key);

/** Session token from the Cookie header; false if none. */
bool gw_http_session_token(httpd_req_t *req, char tok[GW_TOKEN_HEX_LEN + 1]);

/**
 * Sets the session cookie header (token NULL = delete it). secure adds the Secure attribute
 * (HTTPS only). buf must stay alive until the response is sent.
 */
void gw_http_set_session_cookie(httpd_req_t *req, const char *token, bool secure, char buf[160]);

uint32_t gw_http_now_s(void);

/** Restarts the chip after delay_ms, so the HTTP response is delivered first. */
void gw_http_reboot_later(uint32_t delay_ms);
