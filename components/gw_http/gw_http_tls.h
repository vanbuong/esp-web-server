#pragma once
// TLS identity steps shared by the setup wizard and the admin API.
#include <stdbool.h>
#include "esp_err.h"
#include "gw_tls_check.h"

#define GW_HTTP_TLS_BODY_MAX 8192   // key + certificate chain + CA in one JSON body

/**
 * CSR for the pending device key, as a malloc'd PEM string (caller frees).
 * new_key=false reuses the pending key if there is one (same CSR every time); true always makes
 * a fresh key (admin "renew"). The key is stored in NVS as "pending" and never returned.
 */
esp_err_t gw_http_tls_csr(const char *hostname, bool new_key, char **csr_out);

/**
 * Validates and stores an identity. key == NULL means "use the pending key" (CSR path).
 * The strings are normalized in place. On success the pending key is cleared and info filled.
 * Returns NULL, or an error code: those of gw_tls_check(), "no_pending_key", "storage_error".
 */
const char *gw_http_tls_accept(char *key, char *cert, char *ca, const char *hostname,
                               gw_tls_info_t *info);
