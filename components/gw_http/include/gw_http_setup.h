#pragma once
// Setup-mode web server (plain HTTP on port 80, only reachable through the WPA2 setup AP).
// Serves the JSON API of the setup wizard; the web UI arrives in plan 3 (placeholder page now).
#include "esp_err.h"
#include "gw_auth.h"

typedef struct {
    const char *device_id;       // e.g. "1A2B"
    const char *hostname;        // e.g. "gateway1A2B"
    const char *fw_version;
    const gw_creds_t *defaults;  // user, default_password, iterations; pw state is read from gw_store
} gw_http_setup_cfg_t;

/** Starts the server. All strings and *defaults must stay valid while it runs. */
esp_err_t gw_http_setup_start(const gw_http_setup_cfg_t *cfg);
