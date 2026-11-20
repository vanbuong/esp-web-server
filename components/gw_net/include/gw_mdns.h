#pragma once
// mDNS ("multicast DNS"): lets computers on the same network find the device as
// <hostname>.local without any DNS server. We also advertise the web service so tools that
// browse for services (e.g. `dns-sd -B _https._tcp`) can list gateways.
#include <stdbool.h>
#include "esp_err.h"

/** hostname without ".local"; instance = friendly name, e.g. "Gateway 1A2B". https: 443 vs http 80. */
esp_err_t gw_mdns_start(const char *hostname, const char *instance, bool https);
