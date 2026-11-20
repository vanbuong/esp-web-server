#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "mbedtls/oid.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "gw_tls_check.h"

void gw_pem_normalize(char *pem)
{
    char *w = pem;
    for (const char *r = pem; *r; r++) {
        if (*r != '\r') {
            *w++ = *r;
        }
    }
    *w = 0;
    size_t start = 0, n = strlen(pem);
    while (start < n && isspace((unsigned char)pem[start])) {
        start++;
    }
    while (n > start && isspace((unsigned char)pem[n - 1])) {
        n--;
    }
    memmove(pem, pem + start, n - start);
    pem[n - start] = 0;
}

// First CN attribute of a distinguished name.
static void get_cn(const mbedtls_x509_name *n, char *out, size_t len)
{
    out[0] = 0;
    for (; n; n = n->next) {
        if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &n->oid) == 0) {
            size_t l = n->val.len < len - 1 ? n->val.len : len - 1;
            memcpy(out, n->val.p, l);
            out[l] = 0;
            return;
        }
    }
}

// Browsers match names against subjectAltName DNS entries only (the CN is ignored).
static bool has_dns_name(const mbedtls_x509_crt *crt, const char *hostname)
{
    char want[80];
    int wl = snprintf(want, sizeof(want), "%s.local", hostname);
    for (const mbedtls_x509_sequence *s = &crt->subject_alt_names; s && s->buf.p; s = s->next) {
        mbedtls_x509_subject_alternative_name san;
        if (mbedtls_x509_parse_subject_alt_name(&s->buf, &san) != 0) {
            continue;
        }
        bool match = san.type == MBEDTLS_X509_SAN_DNS_NAME &&
                     san.san.unstructured_name.len == (size_t)wl &&
                     strncasecmp((const char *)san.san.unstructured_name.p, want, wl) == 0;
        mbedtls_x509_free_subject_alt_name(&san);
        if (match) {
            return true;
        }
    }
    return false;
}

// True if the certificate's public key is the public half of the private key.
// Compares the DER encodings rather than using mbedtls_pk_check_pair: in mbedTLS 4.1 that only
// works for EC keys (it compares a cached raw public key that RSA contexts don't fill in).
static bool same_public_key(const mbedtls_pk_context *pub, const mbedtls_pk_context *prv)
{
    unsigned char a[800], b[800];   // RSA-4096 public key DER is ~550 bytes
    int la = mbedtls_pk_write_pubkey_der(pub, a, sizeof(a));
    int lb = mbedtls_pk_write_pubkey_der(prv, b, sizeof(b));
    // The DER is written at the END of the buffer.
    return la > 0 && la == lb && memcmp(a + sizeof(a) - la, b + sizeof(b) - lb, la) == 0;
}

static void describe_key(const mbedtls_pk_context *key, char *out, size_t len)
{
    psa_key_type_t t = mbedtls_pk_get_key_type(key);
    size_t bits = mbedtls_pk_get_bitlen(key);
    if (PSA_KEY_TYPE_IS_ECC(t)) {
        snprintf(out, len, PSA_KEY_TYPE_ECC_GET_FAMILY(t) == PSA_ECC_FAMILY_SECP_R1 ? "EC P-%u" : "EC %u",
                 (unsigned)bits);
    } else if (PSA_KEY_TYPE_IS_RSA(t)) {
        snprintf(out, len, "RSA %u", (unsigned)bits);
    } else {
        snprintf(out, len, "other");
    }
}

// PEM parsers want the length including the terminating NUL.
#define PEM_ARGS(s) (const unsigned char *)(s), strlen(s) + 1

const char *gw_tls_check(const char *key_pem, const char *cert_pem, const char *ca_pem,
                         const char *hostname, gw_tls_info_t *info)
{
    mbedtls_pk_context key;
    mbedtls_x509_crt crt, ca;
    mbedtls_pk_init(&key);
    mbedtls_x509_crt_init(&crt);
    mbedtls_x509_crt_init(&ca);
    const char *err = NULL;
    uint32_t flags = 0;
    const char sa[] = MBEDTLS_OID_SERVER_AUTH;

    if (psa_crypto_init() != PSA_SUCCESS || mbedtls_pk_parse_key(&key, PEM_ARGS(key_pem), NULL, 0) != 0) {
        err = "bad_key";
    } else if (mbedtls_x509_crt_parse(&crt, PEM_ARGS(cert_pem)) != 0) {
        err = "bad_cert";
    } else if (mbedtls_x509_crt_parse(&ca, PEM_ARGS(ca_pem)) != 0 || mbedtls_x509_crt_get_ca_istrue(&ca) != 1) {
        err = "bad_ca";
    } else if (!same_public_key(&crt.pk, &key)) {
        err = "key_mismatch";
    } else if (!has_dns_name(&crt, hostname)) {
        err = "wrong_name";
    } else if (mbedtls_x509_crt_check_extended_key_usage(&crt, sa, sizeof(sa) - 1) != 0) {
        err = "not_server_cert";   // (a certificate without the extension is allowed for any use)
    } else if (mbedtls_x509_crt_verify(&crt, &ca, NULL, NULL, &flags, NULL, NULL) != 0) {
        err = "not_signed_by_ca";  // crt->next (intermediates) is used to build the chain
    } else {
        get_cn(&crt.subject, info->subject_cn, sizeof(info->subject_cn));
        get_cn(&crt.issuer, info->issuer_cn, sizeof(info->issuer_cn));
        const mbedtls_x509_time *t = &crt.valid_to;
        // unsigned + modulo: each field has a provable width, so the 20-byte buffer always fits
        snprintf(info->not_after, sizeof(info->not_after), "%04u-%02u-%02u %02u:%02u:%02u",
                 (unsigned)t->year % 10000u, (unsigned)t->mon % 100u, (unsigned)t->day % 100u,
                 (unsigned)t->hour % 100u, (unsigned)t->min % 100u, (unsigned)t->sec % 100u);
        describe_key(&key, info->key_type, sizeof(info->key_type));
    }
    mbedtls_x509_crt_free(&ca);
    mbedtls_x509_crt_free(&crt);
    mbedtls_pk_free(&key);
    return err;
}
