#pragma once
// Captive-portal DNS: in setup mode every name lookup is answered with the setup AP's own IP
// (192.168.4.1). Phones check a known URL after joining a network; getting our page instead
// makes them pop up the setup wizard automatically.
#include "esp_err.h"

/** Starts the DNS server on UDP port 53 of the setup AP. Call after gw_wifi_start_setup(). */
esp_err_t gw_dns_start(void);
