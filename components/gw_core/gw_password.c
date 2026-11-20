#include <string.h>
#include "psa/crypto.h"
#include "gw_password.h"

// ESP-IDF v6.1 ships mbedTLS 4, where crypto goes through the PSA API: PBKDF2 is a
// "key derivation" with inputs given in a fixed order (cost, salt, password).
esp_err_t gw_pw_hash(const char *pw, const uint8_t *salt, size_t salt_len, uint32_t iterations,
                     uint8_t out[GW_PW_HASH_LEN])
{
    if (!pw || !salt || !out || iterations == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {   // safe to call repeatedly
        return ESP_FAIL;
    }
    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
    psa_status_t st = psa_key_derivation_setup(&op, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_input_integer(&op, PSA_KEY_DERIVATION_INPUT_COST, iterations);
    }
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SALT, salt, salt_len);
    }
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                            (const uint8_t *)pw, strlen(pw));
    }
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_output_bytes(&op, out, GW_PW_HASH_LEN);
    }
    psa_key_derivation_abort(&op);
    return st == PSA_SUCCESS ? ESP_OK : ESP_FAIL;
}

bool gw_pw_verify(const char *pw, const uint8_t salt[GW_PW_SALT_LEN],
                  const uint8_t hash[GW_PW_HASH_LEN], uint32_t iterations)
{
    uint8_t got[GW_PW_HASH_LEN];
    if (gw_pw_hash(pw, salt, GW_PW_SALT_LEN, iterations, got) != ESP_OK) {
        return false;
    }
    // Constant-time compare: always looks at every byte, so response time doesn't reveal
    // how many leading bytes matched. (mbedTLS 4 has no public helper for this.)
    uint8_t diff = 0;
    for (size_t i = 0; i < GW_PW_HASH_LEN; i++) {
        diff |= got[i] ^ hash[i];
    }
    return diff == 0;
}

bool gw_pw_policy_ok(const char *pw)
{
    if (!pw) {
        return false;
    }
    size_t n = strnlen(pw, GW_PW_MAX + 1);
    return n >= GW_PW_MIN && n <= GW_PW_MAX;
}
