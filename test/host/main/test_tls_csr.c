#include <string.h>
#include "unity.h"
#include "mbedtls/pk.h"
#include "mbedtls/x509_csr.h"
#include "psa/crypto.h"
#include "gw_tls_csr.h"

static void test_new_key_is_p256(void)
{
    char k1[GW_KEY_PEM_MAX], k2[GW_KEY_PEM_MAX];
    TEST_ASSERT_EQUAL(ESP_OK, gw_tls_new_key(k1, sizeof(k1)));
    TEST_ASSERT_EQUAL(ESP_OK, gw_tls_new_key(k2, sizeof(k2)));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(k1, k2));      // fresh randomness each time
    TEST_ASSERT_NOT_NULL(strstr(k1, "PRIVATE KEY-----"));

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    TEST_ASSERT_EQUAL(0, mbedtls_pk_parse_key(&pk, (const unsigned char *)k1, strlen(k1) + 1, NULL, 0));
    psa_key_type_t t = mbedtls_pk_get_key_type(&pk);
    TEST_ASSERT_TRUE(PSA_KEY_TYPE_IS_ECC_KEY_PAIR(t));
    TEST_ASSERT_EQUAL(PSA_ECC_FAMILY_SECP_R1, PSA_KEY_TYPE_ECC_GET_FAMILY(t));
    TEST_ASSERT_EQUAL(256, mbedtls_pk_get_bitlen(&pk));
    mbedtls_pk_free(&pk);
}

static void test_key_buffer_too_small(void)
{
    char k[32];
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gw_tls_new_key(k, sizeof(k)));
}

static void test_csr_fields(void)
{
    char key[GW_KEY_PEM_MAX], csr[GW_CSR_PEM_MAX];
    TEST_ASSERT_EQUAL(ESP_OK, gw_tls_new_key(key, sizeof(key)));
    TEST_ASSERT_EQUAL(ESP_OK, gw_tls_make_csr(key, "gatewayTEST", csr, sizeof(csr)));
    TEST_ASSERT_NOT_NULL(strstr(csr, "-----BEGIN CERTIFICATE REQUEST-----"));

    mbedtls_x509_csr req;
    mbedtls_pk_context pk;
    mbedtls_x509_csr_init(&req);
    mbedtls_pk_init(&pk);
    TEST_ASSERT_EQUAL(0, mbedtls_x509_csr_parse(&req, (const unsigned char *)csr, strlen(csr) + 1));
    char dn[128];
    TEST_ASSERT_GREATER_THAN(0, mbedtls_x509_dn_gets(dn, sizeof(dn), &req.subject));
    TEST_ASSERT_EQUAL_STRING("CN=gatewayTEST", dn);
    // The CSR carries the public half of our key (EC: check_pair works).
    TEST_ASSERT_EQUAL(0, mbedtls_pk_parse_key(&pk, (const unsigned char *)key, strlen(key) + 1, NULL, 0));
    TEST_ASSERT_EQUAL(0, mbedtls_pk_check_pair(&req.pk, &pk));
    mbedtls_pk_free(&pk);
    mbedtls_x509_csr_free(&req);
}

static void test_csr_san(void)
{
    char key[GW_KEY_PEM_MAX], csr[GW_CSR_PEM_MAX];
    TEST_ASSERT_EQUAL(ESP_OK, gw_tls_new_key(key, sizeof(key)));
    TEST_ASSERT_EQUAL(ESP_OK, gw_tls_make_csr(key, "gatewayTEST", csr, sizeof(csr)));
    mbedtls_x509_csr req;
    mbedtls_x509_csr_init(&req);
    TEST_ASSERT_EQUAL(0, mbedtls_x509_csr_parse(&req, (const unsigned char *)csr, strlen(csr) + 1));
    mbedtls_x509_subject_alternative_name san;
    TEST_ASSERT_NOT_NULL(req.subject_alt_names.buf.p);
    TEST_ASSERT_EQUAL(0, mbedtls_x509_parse_subject_alt_name(&req.subject_alt_names.buf, &san));
    TEST_ASSERT_EQUAL(MBEDTLS_X509_SAN_DNS_NAME, san.type);
    TEST_ASSERT_EQUAL(strlen("gatewayTEST.local"), san.san.unstructured_name.len);
    TEST_ASSERT_EQUAL_MEMORY("gatewayTEST.local", san.san.unstructured_name.p, san.san.unstructured_name.len);
    mbedtls_x509_free_subject_alt_name(&san);
    mbedtls_x509_csr_free(&req);
}

static void test_csr_bad_key(void)
{
    char csr[GW_CSR_PEM_MAX];
    TEST_ASSERT_NOT_EQUAL(ESP_OK, gw_tls_make_csr("garbage", "gatewayTEST", csr, sizeof(csr)));
}

void run_tls_csr_tests(void)
{
    RUN_TEST(test_new_key_is_p256);
    RUN_TEST(test_key_buffer_too_small);
    RUN_TEST(test_csr_fields);
    RUN_TEST(test_csr_san);
    RUN_TEST(test_csr_bad_key);
}
