/**
 * @file c3tout.h
 * @brief Fixed output buffers for the serialized SDK TLS setup path.
 */
#ifndef UTHOME_C3TOUT_H
#define UTHOME_C3TOUT_H
#include <stddef.h>
#include "mbedtls/ssl.h"

/* One application owner, task context and cache enabled. Each initialized TLS
 * context owns its output buffer until SDK cleanup, including WANT_WRITE.
 * The native setup alias retains the unchanged pinned SDK implementation.
 */
int c3tout_setup (mbedtls_ssl_context *      p_ssl,
                  const mbedtls_ssl_config * p_config);
int c3tout_native_setup (mbedtls_ssl_context *      p_ssl,
                         const mbedtls_ssl_config * p_config);

/* Bound only into ssl_tls.c. Only its setup output allocation uses the three
 * fixed buffers, with no fallback on exhaustion. Other requests/releases keep
 * the SDK allocator; this is not whole-TLS static-memory qualification.
 */
void * c3tout_alloc (size_t count, size_t size);
void c3tout_free (void * p_memory);
void c3tout_wipe (void * p_memory, size_t size);
#endif

/* End of file. */

