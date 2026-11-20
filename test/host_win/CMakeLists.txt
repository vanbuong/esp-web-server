# Native Windows build of the host unit tests (MinGW gcc), for when WSL isn't available.
# Builds gw_core + the shared test files from test/host/main against upstream mbedTLS, Unity and
# cJSON taken from an ESP-IDF install. The NVS storage tests are left out: they need ESP-IDF's
# POSIX flash emulation (run them with tools/host_test.ps1 in WSL).
# Driven by tools/host_test_win.ps1.
cmake_minimum_required(VERSION 3.16)
project(host_test_win C)

set(GW_IDF_PATH "" CACHE PATH "ESP-IDF root (for mbedtls, unity, esp_common headers)")
set(GW_CJSON_DIR "" CACHE PATH "folder containing cJSON.c / cJSON.h")
if(NOT EXISTS "${GW_IDF_PATH}/components/mbedtls/mbedtls/CMakeLists.txt")
    message(FATAL_ERROR "GW_IDF_PATH must point to an ESP-IDF tree (got '${GW_IDF_PATH}')")
endif()
if(NOT EXISTS "${GW_CJSON_DIR}/cJSON.c")
    message(FATAL_ERROR "GW_CJSON_DIR must contain cJSON.c (got '${GW_CJSON_DIR}')")
endif()

set(REPO "${CMAKE_CURRENT_LIST_DIR}/../..")
set(FIX "${REPO}/test/host/fixtures")
set(C "${GW_IDF_PATH}/components")

# --- upstream mbedTLS (the copy ESP-IDF ships; its default config, not ESP-IDF's) ------------
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
set(DISABLE_PACKAGE_CONFIG_AND_INSTALL ON CACHE BOOL "" FORCE)
# ESP-IDF's copy includes a few headers that ESP-IDF itself provides (port/include); stand-ins:
include_directories(BEFORE "${CMAKE_CURRENT_LIST_DIR}/mbedtls_shim")
add_subdirectory("${C}/mbedtls/mbedtls" mbedtls EXCLUDE_FROM_ALL)

# --- fixtures: the PEM files the tests embed, turned into C arrays with the same symbol names
# that ESP-IDF's EMBED_TXTFILES produces (_binary_<name>_start) ------------------------------
set(FIX_NAMES ca.crt ca2.crt dev.key dev.crt dev_pkcs8.key dev_rsa.key dev_rsa.crt dev_other_name.crt
              dev_client_eku.crt dev_by_ca2.crt int.crt dev_via_int.key dev_via_int.crt client.key)
set(FIX_C "${CMAKE_CURRENT_BINARY_DIR}/fixtures.c")
set(FIX_SRC "/* generated from test/host/fixtures by test/host_win/CMakeLists.txt */\n")
foreach(f ${FIX_NAMES})
    if(NOT EXISTS "${FIX}/${f}")
        message(FATAL_ERROR "missing fixture ${FIX}/${f} — run tools/pki/make_test_fixtures.sh")
    endif()
    string(MAKE_C_IDENTIFIER "${f}" id)
    file(READ "${FIX}/${f}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(APPEND FIX_SRC "const char fx_${id}[] __asm__(\"_binary_${id}_start\") = {${bytes}0};\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FIX}/${f}")
endforeach()
file(WRITE "${FIX_C}" "${FIX_SRC}")

# --- the test program ----------------------------------------------------------------------
set(CORE "${REPO}/components/gw_core")
set(TESTS "${REPO}/test/host/main")
add_executable(host_test_win
    "${TESTS}/test_main.c" "${TESTS}/test_dev_id.c" "${TESTS}/test_password.c" "${TESTS}/test_auth.c"
    "${TESTS}/test_setup.c" "${TESTS}/test_wifi_util.c" "${TESTS}/test_tls_check.c" "${TESTS}/test_tls_csr.c"
    "${CORE}/gw_dev_id.c" "${CORE}/gw_password.c" "${CORE}/gw_auth.c" "${CORE}/gw_setup.c"
    "${CORE}/gw_wifi_util.c" "${CORE}/gw_tls_check.c" "${CORE}/gw_tls_csr.c"
    "${C}/unity/unity/src/unity.c"
    "${GW_CJSON_DIR}/cJSON.c"
    "${FIX_C}")
target_include_directories(host_test_win PRIVATE
    "${CORE}/include" "${C}/esp_common/include" "${CMAKE_CURRENT_LIST_DIR}/idf_shim"
    "${C}/unity/unity/src" "${GW_CJSON_DIR}")
target_compile_definitions(host_test_win PRIVATE GW_HOST_NO_NVS)
target_compile_options(host_test_win PRIVATE -Wall -Wno-unused-parameter)
target_link_libraries(host_test_win PRIVATE mbedx509 mbedtls tfpsacrypto)
if(WIN32)
    target_link_libraries(host_test_win PRIVATE bcrypt)   # PSA entropy uses BCryptGenRandom
endif()
