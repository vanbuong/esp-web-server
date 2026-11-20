#pragma once
// Handlers and state shared by the setup server (HTTP, setup mode) and the admin server
// (HTTPS, normal mode). Only one of the two runs per boot, so the state is module-global.
#include <stdbool.h>
#include "esp_http_server.h"
#include "cJSON.h"
#include "gw_auth.h"
#include "gw_tls_check.h"

typedef struct {
    bool normal;                  // false = setup mode (HTTP), true = normal mode (HTTPS)
    const char *device_id, *hostname, *fw_version;
    const gw_creds_t *defaults;   // user, default password, PBKDF2 iterations
    gw_auth_t *auth;
    // Normal mode only: adds fields to /api/status (logged_in tells whether details are allowed).
    void (*status_extra)(httpd_req_t *req, cJSON *status, bool logged_in);
} gw_http_ctx_t;

extern gw_http_ctx_t g_http;

/** Fills g_http and creates the session table. Strings must outlive the server. */
esp_err_t gw_http_common_init(bool normal, const char *device_id, const char *hostname,
                              const char *fw_version, const gw_creds_t *defaults);

/** Registers "/", GET /api/status, POST /api/login, POST /api/logout. */
esp_err_t gw_http_common_register(httpd_handle_t srv);

bool gw_http_logged_in(httpd_req_t *req);

/** Kconfig defaults + the stored password (if one was set). */
esp_err_t gw_http_load_creds(gw_creds_t *c);

/**
 * Hashes and stores new_pw (fresh salt), ends every session and gives the caller a new one
 * (cookie header set on req, using cookie_buf). The caller sends the response.
 */
esp_err_t gw_http_change_password(httpd_req_t *req, const char *new_pw, char cookie_buf[160]);
