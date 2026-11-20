// Test entry point, shared by two builds:
//  - ESP-IDF "linux" target in WSL (test/host): the full suite, entered through app_main().
//  - Native Windows with MinGW gcc (test/host_win): everything except the NVS storage tests,
//    which need ESP-IDF's POSIX flash emulation (GW_HOST_NO_NVS), entered through main().
#include <stdlib.h>
#include <time.h>
#include "unity.h"

#ifdef ESP_PLATFORM
#include "mbedtls/platform_time.h"

// linux target only: ESP-IDF's mbedTLS port (esp_platform_time.c) isn't built for the host, so
// x509 code that needs a millisecond clock gets this one. Firmware uses the real port, and the
// Windows build uses plain upstream mbedTLS, which brings its own.
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (mbedtls_ms_time_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
#endif

// Each test_*.c file exposes one run_*_tests() that calls RUN_TEST for its cases.
void run_dev_id_tests(void);
void run_password_tests(void);
void run_auth_tests(void);
void run_setup_tests(void);
void run_wifi_util_tests(void);
void run_store_tests(void);
void run_tls_check_tests(void);
void run_tls_csr_tests(void);

void setUp(void) {}
void tearDown(void) {}

static int run_all(void)
{
    UNITY_BEGIN();
    run_dev_id_tests();
    run_password_tests();
    run_auth_tests();
    run_setup_tests();
    run_wifi_util_tests();
#ifndef GW_HOST_NO_NVS
    run_store_tests();
#endif
    run_tls_check_tests();
    run_tls_csr_tests();
    return UNITY_END();   // number of failures, so scripts can check the exit code
}

#ifdef ESP_PLATFORM
void app_main(void)
{
    exit(run_all());
}
#else
int main(void)
{
    return run_all();
}
#endif
