#include <stdlib.h>
#include "unity.h"

// Each test_*.c file exposes one run_*_tests() that calls RUN_TEST for its cases.
void run_dev_id_tests(void);
void run_password_tests(void);
void run_auth_tests(void);
void run_setup_tests(void);
void run_wifi_util_tests(void);
void run_store_tests(void);

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
    exit(UNITY_END());   // exit code = number of failures, so scripts can check it
}
