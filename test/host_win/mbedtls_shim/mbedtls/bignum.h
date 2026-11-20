// ESP-IDF's mbedTLS copy includes "mbedtls/bignum.h", which ESP-IDF supplies in
// components/mbedtls/port/include (adding hardware-acceleration hooks). A PC build has no such
// hardware, so this shim does only what that wrapper does first.
#pragma once
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#include "mbedtls/private/bignum.h"
