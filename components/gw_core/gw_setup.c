#include "gw_setup.h"

bool gw_setup_allowed(gw_step_t current, gw_step_t requested)
{
    return current != GW_STEP_DONE && requested <= current;
}

gw_step_t gw_setup_complete(gw_step_t current, gw_step_t completed)
{
    return (completed == current && current != GW_STEP_DONE) ? current + 1 : current;
}

const char *gw_setup_step_name(gw_step_t s)
{
    static const char *const names[] = {"password", "wifi", "tls", "finish", "done"};
    return (unsigned)s <= GW_STEP_DONE ? names[s] : "unknown";
}

gw_step_t gw_setup_sanitize(uint8_t stored)
{
    return stored <= GW_STEP_DONE ? (gw_step_t)stored : GW_STEP_PASSWORD;
}
