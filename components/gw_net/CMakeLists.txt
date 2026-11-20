# gw_net: radio-facing glue (Wi-Fi, captive DNS, mDNS). Target only; logic it needs is in gw_core.
idf_component_register(SRCS "gw_wifi.c" "gw_dns.c" "gw_mdns.c" "dns_server/dns_server.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "dns_server"
                       REQUIRES gw_core esp_wifi
                       PRIV_REQUIRES esp_netif esp_event esp_timer espressif__mdns)
