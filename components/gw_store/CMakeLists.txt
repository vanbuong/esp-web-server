idf_component_register(SRCS "gw_store.c"
                       INCLUDE_DIRS "include"
                       REQUIRES gw_core
                       PRIV_REQUIRES nvs_flash)
