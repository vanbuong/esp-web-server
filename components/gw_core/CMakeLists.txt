# gw_core: pure logic only (no Wi-Fi, HTTP or GPIO includes), so it also builds for the
# "linux" target and is unit-tested on the PC.
idf_component_register(SRCS "gw_dev_id.c"
                            "gw_password.c"
                            "gw_auth.c"
                            "gw_setup.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_common
                       PRIV_REQUIRES mbedtls)
