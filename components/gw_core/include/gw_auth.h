#pragma once
// Login decisions and session tokens, with no HTTP or clock dependency: callers pass the
// current time and a random source, so this runs (and is tested) on the PC too.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gw_password.h"

#define GW_TOKEN_HEX_LEN 64      // 32 random bytes, hex: unguessable session id
#define GW_LOGIN_MAX_FAILS 5     // consecutive failures before lockout
#define GW_LOGIN_LOCK_S 30       // lockout length

typedef struct {
    const char *user;              // fixed admin user name
    const char *default_password;  // valid only while !pw_set
    bool pw_set;
    uint8_t salt[GW_PW_SALT_LEN];
    uint8_t hash[GW_PW_HASH_LEN];
    uint32_t iterations;
} gw_creds_t;

typedef struct gw_auth gw_auth_t;  // opaque; owns the session table and the login limiter
typedef enum { GW_LOGIN_OK, GW_LOGIN_BAD, GW_LOGIN_LOCKED } gw_login_result_t;
typedef void (*gw_rand_fn)(uint8_t *buf, size_t len);   // esp_fill_random on target, fake in tests

/** NULL on allocation failure. timeout_s = session idle timeout. */
gw_auth_t *gw_auth_create(size_t max_sessions, uint32_t timeout_s, gw_rand_fn rnd);
void gw_auth_destroy(gw_auth_t *a);

/**
 * Checks user/password and on success creates a session (evicting the least recently used one
 * if the table is full). The limiter is global: GW_LOGIN_MAX_FAILS consecutive failures lock
 * every login for GW_LOGIN_LOCK_S seconds, even correct ones; a success resets the count.
 * The default password works only while c->pw_set is false.
 * @param retry_after_s set when the result is GW_LOGIN_LOCKED (may be NULL).
 */
gw_login_result_t gw_auth_login(gw_auth_t *a, const gw_creds_t *c, const char *user, const char *pw,
                                uint32_t now_s, char token_out[GW_TOKEN_HEX_LEN + 1],
                                uint32_t *retry_after_s);

/** True if token is a live session; refreshes its idle timer. Expired sessions are dropped. */
bool gw_auth_check(gw_auth_t *a, const char *token, uint32_t now_s);
void gw_auth_logout(gw_auth_t *a, const char *token);
void gw_auth_logout_all(gw_auth_t *a);   // after a password change

/** Finds cookie `name` in a Cookie header. False if absent or the value doesn't fit out. */
bool gw_cookie_get(const char *cookie_header, const char *name, char *out, size_t out_len);
