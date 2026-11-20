#include <stdio.h>
#include <string.h>
#include "mbedtls/pk.h"
#include "mbedtls/x509_csr.h"
#include "psa/crypto.h"
#include "gw_tls_csr.h"

esp_err_t gw_tls_new_key(char *key_pem, size_t len)
{
    if (psa_crypto_init() != PSA_SUCCESS) {
        return ESP_FAIL;
    }
    // mbedTLS 4: keys are made by PSA. EXPORT is needed once, to turn the key into PEM for NVS.
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    mbedtls_svc_key_id_t id;
    if (psa_generate_key(&attr, &id) != PSA_SUCCESS) {
        return ESP_FAIL;
    }
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_copy_from_psa(id, &pk);
    if (ret == 0) {
        ret = mbedtls_pk_write_key_pem(&pk, (unsigned char *)key_pem, len);
    }
    mbedtls_pk_free(&pk);
    psa_destroy_key(id);   // the PEM copy is what we keep; wipe the PSA one
    return ret == 0 ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

esp_err_t gw_tls_make_csr(const char *key_pem, const char *hostname, char *csr_pem, size_t len)
{
    char cn[80], dns[80];
    snprintf(cn, sizeof(cn), "CN=%s", hostname);
    int dl = snprintf(dns, sizeof(dns), "%s.local", hostname);

    mbedtls_pk_context pk;
    mbedtls_x509write_csr req;
    mbedtls_pk_init(&pk);
    mbedtls_x509write_csr_init(&req);
    int ret = psa_crypto_init() == PSA_SUCCESS ? 0 : -1;
    if (ret == 0) {
        ret = mbedtls_pk_parse_key(&pk, (const unsigned char *)key_pem, strlen(key_pem) + 1, NULL, 0);
    }
    if (ret == 0) {
        mbedtls_x509write_csr_set_md_alg(&req, MBEDTLS_MD_SHA256);
        mbedtls_x509write_csr_set_key(&req, &pk);
        ret = mbedtls_x509write_csr_set_subject_name(&req, cn);
    }
    if (ret == 0) {
        // The CA (tools/pki/sign_csr) sets the final name itself; this is a hint for other CAs.
        mbedtls_x509_san_list san = {
            .node = {.type = MBEDTLS_X509_SAN_DNS_NAME,
                     .san.unstructured_name = {.p = (unsigned char *)dns, .len = (size_t)dl}},
            .next = NULL,
        };
        ret = mbedtls_x509write_csr_set_subject_alternative_name(&req, &san);
    }
    if (ret == 0) {
        ret = mbedtls_x509write_csr_pem(&req, (unsigned char *)csr_pem, len);
    }
    mbedtls_x509write_csr_free(&req);
    mbedtls_pk_free(&pk);
    return ret == 0 ? ESP_OK : ESP_FAIL;
}
