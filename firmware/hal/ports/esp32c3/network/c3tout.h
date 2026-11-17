/**
 * @file c3tout.h
 * @brief Fixed MQTT TLS records for serialized SDK calls.
 */
#ifndef UTHOME_C3TOUT_H
#define UTHOME_C3TOUT_H
#include <stddef.h>
#include "mbedtls/ssl.h"

/* One application owner, task context and cache enabled. Each initialized TLS
 * context owns its input/output buffers until SDK cleanup, including
 * WANT_READ/WANT_WRITE. Setup requires an initialized, unused SDK context.
 * The native setup alias retains the unchanged pinned SDK implementation.
 */
int c3tout_setup (mbedtls_ssl_context *      p_ssl,
                  const mbedtls_ssl_config * p_config);
int c3tout_native_setup (mbedtls_ssl_context *      p_ssl,
                         const mbedtls_ssl_config * p_config);

/* Both native renegotiation entries can initialize state. The SDK retains old
 * active transform/session records while negotiating replacements. Two of each
 * and one handshake record are reserved. Wrappers are synchronous, nonrecursive
 * and require the same single owner as setup; SDK callbacks must not reenter.
 * Renegotiation retries preserve the native SDK path without arming storage.
 */
int c3tout_start (mbedtls_ssl_context * p_ssl);
int c3tout_native_start (mbedtls_ssl_context * p_ssl);
int c3tout_renegotiate (mbedtls_ssl_context * p_ssl);
int c3tout_native_renegotiate (mbedtls_ssl_context * p_ssl);

/* One temporary typed chain head and one retained peer digest follow the native
 * parser's allocation/free order. The SDK frees a matching old digest before
 * replacement during renegotiation; mismatches and later failures retain their
 * native semantics. Inner certificates/keys still use SDK allocation. Parsing
 * is synchronous and must not reenter this wrapper through callbacks.
 */
int c3tout_certificate (mbedtls_ssl_context * p_ssl);
int c3tout_native_certificate (mbedtls_ssl_context * p_ssl);

/* Copy at most 253 hostname characters, matching MQTT admission. The borrowed
 * name must remain immutable through this call and must not alias SDK storage.
 * NULL retains the SDK's clear-name behavior. Invalid length preserves the old
 * name; slot exhaustion fails without heap fallback. The SDK owns
 * flags/copying.
 */
int c3tout_set_name (mbedtls_ssl_context * p_ssl, const char * p_hostname);
int c3tout_native_name (mbedtls_ssl_context * p_ssl, const char * p_hostname);

/* Bound only into ssl_tls.c. Its setup uses one input and one output buffer,
 * its scoped hostname setter uses one persistent name, and setup/renegotiation
 * initialize typed state records in the pinned SDK order. Certificate parsing
 * selects its temporary chain head and retained digest. Fixed leases have
 * no heap fallback on exhaustion. Other requests keep
 * the SDK allocator; this is not whole-TLS static-memory qualification.
 */
void * c3tout_alloc (size_t count, size_t size);
void c3tout_free (void * p_memory);
void c3tout_wipe (void * p_memory, size_t size);
#endif

/* End of file. */

