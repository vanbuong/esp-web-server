idf_component_register(SRCS "main.c"
                       # espcoredump: listed so MINIMAL_BUILD includes it and crash dumps go to flash
                       PRIV_REQUIRES gw_core gw_store gw_net esp_app_format espcoredump esp_hw_support)
