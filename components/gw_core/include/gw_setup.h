#pragma once
// Setup wizard order, enforced by the device (not just the web page): a request for a later
// step than the current one is refused, so typing a URL can't skip the password change.
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GW_STEP_PASSWORD = 0,
    GW_STEP_WIFI = 1,
    GW_STEP_TLS = 2,
    GW_STEP_FINISH = 3,
    GW_STEP_DONE = 4,
} gw_step_t;

/** Earlier steps may be redone; later ones are refused; nothing is allowed once DONE. */
bool gw_setup_allowed(gw_step_t current, gw_step_t requested);

/** Completing the current step moves to the next; completing a redone earlier step doesn't. */
gw_step_t gw_setup_complete(gw_step_t current, gw_step_t completed);

/** "password", "wifi", "tls", "finish", "done"; "unknown" otherwise. */
const char *gw_setup_step_name(gw_step_t s);

/** Value read from NVS → valid step; anything out of range restarts at PASSWORD. */
gw_step_t gw_setup_sanitize(uint8_t stored);
