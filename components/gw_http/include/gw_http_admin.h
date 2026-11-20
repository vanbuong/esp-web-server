#pragma once
// Normal-mode web servers: the HTTPS admin server (port 443) and a plain-HTTP server on port 80
// whose only job is to redirect browsers to HTTPS.
#include "esp_err.h"
#include "gw_auth.h"

typedef struct {
    const char *device_id, *hostname, *fw_version;
    const gw_creds_t *defaults;
    const char *key_pem, *cert_pem, *ca_pem;   // stored identity; must outlive the server
} gw_http_admin_cfg_t;

/**
 * Starts HTTPS. With CONFIG_GW_TLS_MODE_MUTUAL the CA is also used to require a client
 * certificate: browsers without one signed by your CA fail the handshake and see nothing.
 * Returns ESP_ERR_INVALID_STATE if the stored identity no longer validates.
 */
esp_err_t gw_http_admin_start(const gw_http_admin_cfg_t *cfg);

/** HTTP :80 → "301 https://<hostname>.local<same path>". */
esp_err_t gw_http_redirect_start(const char *hostname);
