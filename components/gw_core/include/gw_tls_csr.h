#pragma once
// The device's own TLS key and certificate request (CSR). The key is generated here and never
// leaves the device: you take the CSR to your CA (tools/pki/sign_csr), and upload only the
// signed certificate back.
#include <stddef.h>
#include "esp_err.h"

#define GW_KEY_PEM_MAX 512
#define GW_CSR_PEM_MAX 1024

/** New ECDSA P-256 private key as PEM (PSA generate → PEM). */
esp_err_t gw_tls_new_key(char *key_pem, size_t len);

/** CSR signed with key_pem: subject CN=<hostname>, subjectAltName DNS:<hostname>.local, SHA-256. */
esp_err_t gw_tls_make_csr(const char *key_pem, const char *hostname, char *csr_pem, size_t len);
