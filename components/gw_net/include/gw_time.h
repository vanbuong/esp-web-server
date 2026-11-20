#pragma once
// Wall-clock time over SNTP. Only used to *display* dates (certificate expiry); nothing depends
// on it, because the gateway may sit on a network without internet access.
#include <stdbool.h>
#include "esp_err.h"

/** Starts SNTP; it keeps retrying in the background until the network can reach the server. */
esp_err_t gw_time_start(const char *server);

/** True once the clock has been set (year >= 2025). */
bool gw_time_valid(void);
