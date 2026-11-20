#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gw_auth.h"

typedef struct {
    bool used;
    uint32_t last;                      // last activity, for idle timeout and LRU eviction
    char tok[GW_TOKEN_HEX_LEN + 1];
} slot_t;

struct gw_auth {
    size_t max;
    uint32_t timeout_s;
    gw_rand_fn rnd;
    uint8_t fails;                      // consecutive failed logins
    bool locked;
    uint32_t locked_until;
    slot_t slots[];
};

gw_auth_t *gw_auth_create(size_t max_sessions, uint32_t timeout_s, gw_rand_fn rnd)
{
    if (max_sessions == 0 || !rnd) {
        return NULL;
    }
    gw_auth_t *a = calloc(1, sizeof(*a) + max_sessions * sizeof(slot_t));
    if (a) {
        a->max = max_sessions;
        a->timeout_s = timeout_s;
        a->rnd = rnd;
    }
    return a;
}

void gw_auth_destroy(gw_auth_t *a)
{
    free(a);
}

// Constant time for equal-length strings: timing never says how much of a token matched.
static bool ct_equal(const char *x, const char *y, size_t n)
{
    unsigned char diff = 0;
    for (size_t i = 0; i < n; i++) {
        diff |= (unsigned char)(x[i] ^ y[i]);
    }
    return diff == 0;
}

static bool creds_match(const gw_creds_t *c, const char *user, const char *pw)
{
    if (!user || !pw || strcmp(user, c->user) != 0) {
        return false;
    }
    if (!c->pw_set) {
        return strcmp(pw, c->default_password) == 0;   // public default, nothing to hide
    }
    return gw_pw_verify(pw, c->salt, c->hash, c->iterations);
}

static slot_t *new_slot(gw_auth_t *a)
{
    slot_t *lru = &a->slots[0];
    for (size_t i = 0; i < a->max; i++) {
        if (!a->slots[i].used) {
            return &a->slots[i];
        }
        if (a->slots[i].last < lru->last) {
            lru = &a->slots[i];
        }
    }
    return lru;   // table full: replace the least recently used session
}

gw_login_result_t gw_auth_login(gw_auth_t *a, const gw_creds_t *c, const char *user, const char *pw,
                                uint32_t now_s, char token_out[GW_TOKEN_HEX_LEN + 1],
                                uint32_t *retry_after_s)
{
    if (a->locked) {
        if (now_s < a->locked_until) {
            if (retry_after_s) {
                *retry_after_s = a->locked_until - now_s;
            }
            return GW_LOGIN_LOCKED;
        }
        a->locked = false;
        a->fails = 0;
    }
    if (!creds_match(c, user, pw)) {
        if (++a->fails >= GW_LOGIN_MAX_FAILS) {
            a->locked = true;
            a->locked_until = now_s + GW_LOGIN_LOCK_S;
        }
        return GW_LOGIN_BAD;
    }
    a->fails = 0;

    uint8_t rnd[GW_TOKEN_HEX_LEN / 2];
    a->rnd(rnd, sizeof(rnd));
    slot_t *s = new_slot(a);
    for (size_t i = 0; i < sizeof(rnd); i++) {
        snprintf(&s->tok[2 * i], 3, "%02x", rnd[i]);
    }
    s->used = true;
    s->last = now_s;
    memcpy(token_out, s->tok, GW_TOKEN_HEX_LEN + 1);
    return GW_LOGIN_OK;
}

static slot_t *find(gw_auth_t *a, const char *token)
{
    if (!token || strnlen(token, GW_TOKEN_HEX_LEN + 1) != GW_TOKEN_HEX_LEN) {
        return NULL;
    }
    for (size_t i = 0; i < a->max; i++) {
        if (a->slots[i].used && ct_equal(a->slots[i].tok, token, GW_TOKEN_HEX_LEN)) {
            return &a->slots[i];
        }
    }
    return NULL;
}

bool gw_auth_check(gw_auth_t *a, const char *token, uint32_t now_s)
{
    slot_t *s = find(a, token);
    if (!s) {
        return false;
    }
    if (now_s - s->last > a->timeout_s) {
        memset(s, 0, sizeof(*s));   // expired
        return false;
    }
    s->last = now_s;
    return true;
}

void gw_auth_logout(gw_auth_t *a, const char *token)
{
    slot_t *s = find(a, token);
    if (s) {
        memset(s, 0, sizeof(*s));
    }
}

void gw_auth_logout_all(gw_auth_t *a)
{
    memset(a->slots, 0, a->max * sizeof(slot_t));
}

bool gw_cookie_get(const char *cookie_header, const char *name, char *out, size_t out_len)
{
    if (!cookie_header || !name || !out || out_len == 0) {
        return false;
    }
    size_t nlen = strlen(name);
    const char *p = cookie_header;
    while (*p) {
        while (*p == ' ' || *p == ';') {
            p++;
        }
        const char *end = strchr(p, ';');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len > nlen && strncmp(p, name, nlen) == 0 && p[nlen] == '=') {
            size_t vlen = len - nlen - 1;
            if (vlen >= out_len) {
                return false;
            }
            memcpy(out, p + nlen + 1, vlen);
            out[vlen] = 0;
            return true;
        }
        p += len;
    }
    return false;
}
