idf_component_register(SRCS "test_main.c"
                            "test_dev_id.c"
                            "test_password.c"
                       REQUIRES unity gw_core)

# linux target only: libtfpsacrypto.a needs esp_mbedtls_mem_calloc/free from the IDF mbedtls
# component lib, which the linker has already passed by then (link order). -u makes the linker
# pull them in up front. Firmware (target) builds link in a group and don't need this.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u esp_mbedtls_mem_calloc" "-u esp_mbedtls_mem_free")
