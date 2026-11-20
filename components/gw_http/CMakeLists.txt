# gw_http: thin HTTP glue over gw_core logic. Target only.
idf_component_register(SRCS "gw_http_setup.c" "gw_http_util.c" "gw_http_tls.c" "gw_http_common.c"
                            "gw_http_admin.c" "gw_http_admin_api.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gw_core esp_http_server esp_https_server
                       PRIV_REQUIRES gw_store gw_net esp_timer espressif__cjson esp-tls mbedtls
                       EMBED_TXTFILES "www_placeholder.html")
