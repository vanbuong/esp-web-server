#pragma once
// Admin password storage: only a salted PBKDF2-HMAC-SHA256 hash is kept, never the password.
// Why PBKDF2: it is deliberately slow (many HMAC rounds), so a leaked hash is expensive to
// brute-force; the random salt makes identical passwords hash differently on each device.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define GW_PW_SALT_LEN 16
#define GW_PW_HASH_LEN 32
#define GW_PW_MIN 8
#define GW_PW_MAX 64

/** out = PBKDF2-HMAC-SHA256(pw, salt, iterations), 32 bytes. ESP_FAIL on crypto error. */
esp_err_t gw_pw_hash(const char *pw, const uint8_t *salt, size_t salt_len, uint32_t iterations,
                     uint8_t out[GW_PW_HASH_LEN]);

/** True if pw hashes to hash. Compares in constant time so timing leaks nothing. */
bool gw_pw_verify(const char *pw, const uint8_t salt[GW_PW_SALT_LEN],
                  const uint8_t hash[GW_PW_HASH_LEN], uint32_t iterations);

/** New-password rule: 8..64 bytes (any bytes, UTF-8 welcome). NULL → false. */
bool gw_pw_policy_ok(const char *pw);
