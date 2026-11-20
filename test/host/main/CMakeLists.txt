# Throwaway PKI fixtures made by tools/pki/make_test_fixtures.sh (run by tools/wsl/host_test.sh).
set(fx "${CMAKE_CURRENT_LIST_DIR}/../fixtures")

idf_component_register(SRCS "test_main.c"
                            "test_dev_id.c"
                            "test_password.c"
                            "test_auth.c"
                            "test_setup.c"
                            "test_wifi_util.c"
                            "test_store.c"
                            "test_tls_check.c"
                       REQUIRES unity gw_core gw_store nvs_flash espressif__cjson mbedtls
                       EMBED_TXTFILES "${fx}/ca.crt" "${fx}/dev.key" "${fx}/dev.crt" "${fx}/dev_pkcs8.key"
                                      "${fx}/dev_rsa.key" "${fx}/dev_rsa.crt" "${fx}/dev_other_name.crt"
                                      "${fx}/dev_client_eku.crt" "${fx}/dev_by_ca2.crt" "${fx}/int.crt"
                                      "${fx}/dev_via_int.key" "${fx}/dev_via_int.crt" "${fx}/client.key")

# linux target only: libtfpsacrypto.a needs esp_mbedtls_mem_calloc/free from the IDF mbedtls
# component lib, which the linker has already passed by then (link order). -u makes the linker
# pull them in up front. Firmware (target) builds link in a group and don't need this.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u esp_mbedtls_mem_calloc" "-u esp_mbedtls_mem_free")
