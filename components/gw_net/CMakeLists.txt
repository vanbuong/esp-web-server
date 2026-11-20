# gw_net: radio-facing glue (Wi-Fi, captive DNS, mDNS). Target only; logic it needs is in gw_core.
idf_component_register(SRCS "gw_wifi.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gw_core esp_wifi
                       PRIV_REQUIRES esp_netif esp_event esp_timer)
