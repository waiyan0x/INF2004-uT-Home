/**
 * @file psaconf.h
 * @brief Configure the Wi-Fi station crypto memory footprint.
 */
#ifndef UTHOME_PSACONF_H
#define UTHOME_PSACONF_H

#include <psa/crypto_config.h>

#undef MBEDTLS_PSA_KEY_STORE_DYNAMIC
#define MBEDTLS_PSA_KEY_SLOT_COUNT 32

/* Limit the temporary point table during elliptic-curve multiplication. */
#define MBEDTLS_ECP_WINDOW_SIZE 2

#endif

/* End of file. */

