# gw_http: thin HTTP glue over gw_core logic. Target only.
idf_component_register(SRCS "gw_http_setup.c" "gw_http_util.c" "gw_http_tls.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gw_core esp_http_server
                       PRIV_REQUIRES gw_store gw_net esp_timer espressif__cjson
                       EMBED_TXTFILES "www_placeholder.html")
