#include <stdlib.h>
#include <time.h>
#include "unity.h"
#include "mbedtls/platform_time.h"

// linux target only: ESP-IDF's mbedTLS port (esp_platform_time.c) isn't built for the host, so
// x509 code that needs a millisecond clock gets this one. Firmware uses the real port.
mbedtls_ms_time_t mbedtls_ms_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (mbedtls_ms_time_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Each test_*.c file exposes one run_*_tests() that calls RUN_TEST for its cases.
void run_dev_id_tests(void);
void run_password_tests(void);
void run_auth_tests(void);
void run_setup_tests(void);
void run_wifi_util_tests(void);
void run_store_tests(void);
void run_tls_check_tests(void);

void setUp(void) {}
void tearDown(void) {}

void app_main(void)
{
    UNITY_BEGIN();
    run_dev_id_tests();
    run_password_tests();
    run_auth_tests();
    run_setup_tests();
    run_wifi_util_tests();
    run_store_tests();
    run_tls_check_tests();
    exit(UNITY_END());   // exit code = number of failures, so scripts can check it
}
