/**
 * @file psaconf.h
 * @brief Use the native fixed PSA key table for the Wi-Fi station.
 */
#ifndef UTHOME_PSACONF_H
#define UTHOME_PSACONF_H

#include <psa/crypto_config.h>

#undef MBEDTLS_PSA_KEY_STORE_DYNAMIC
#define MBEDTLS_PSA_KEY_SLOT_COUNT 32

#endif

/* End of file. */

