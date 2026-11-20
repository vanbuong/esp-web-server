#pragma once
// Checks a server identity (private key + certificate + CA) before the gateway stores it.
// Run on the device when you upload certificates, and unit-tested on the PC.
#include <stddef.h>

#define GW_TLS_PEM_MAX 4096   // per field (key, cert chain, CA)

typedef struct {
    char subject_cn[64];
    char issuer_cn[64];
    char not_after[20];       // "YYYY-MM-DD HH:MM:SS" (UTC) as written in the cert
    char key_type[12];        // "EC P-256", "RSA 2048", ...
} gw_tls_info_t;

/**
 * Returns NULL if the identity is usable (info filled), else a stable error code:
 *  "bad_key"           private key doesn't parse
 *  "bad_cert"          certificate (chain) doesn't parse
 *  "bad_ca"            CA doesn't parse or isn't a CA certificate
 *  "key_mismatch"      the key doesn't belong to the certificate
 *  "wrong_name"        the certificate isn't for <hostname>.local (subjectAltName)
 *  "not_server_cert"   extended key usage doesn't allow TLS server use
 *  "not_signed_by_ca"  the certificate doesn't chain up to the CA
 * Dates are deliberately not checked: the device may have no clock.
 * cert_pem may hold a chain: the device certificate first, then intermediates.
 */
const char *gw_tls_check(const char *key_pem, const char *cert_pem, const char *ca_pem,
                         const char *hostname, gw_tls_info_t *info);

/** In place: CRLF → LF and surrounding whitespace removed (pasted PEM is often messy). */
void gw_pem_normalize(char *pem);
