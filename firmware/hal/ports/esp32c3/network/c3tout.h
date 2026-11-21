/**
 * @file c3tout.h
 * @brief Fixed MQTT TLS records for serialized SDK calls.
 */
#ifndef UTHOME_C3TOUT_H
#define UTHOME_C3TOUT_H
#include <stddef.h>
#include <stdint.h>
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"

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
void * c3tout_name_node (const mbedtls_x509_crt * p_chain);
void c3tout_node_free (void * p_memory);
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

/* Source-bound ssl_tls.c PRF inputs use a 64-byte seed, a 22-byte label and
 * separate imported-key and secret copies sized for the master and premaster.
 * PSA validates/copies inputs and owns abort/cleanup. Input and operation must
 * satisfy the native PSA API; this synchronous single-owner wrapper cannot be
 * reentered. The selected TLS handshake has no arbitrary exporter/PRF callers.
 * PSA owns its key slot until destruction or failed-import cleanup.
 */
psa_status_t c3tout_prf_input (psa_key_derivation_operation_t * p_operation,
                               psa_key_derivation_step_t        step,
                               const uint8_t *                  p_data,
                               size_t                           size);
psa_status_t c3tout_prf_key (psa_key_derivation_operation_t * p_operation,
                             psa_key_derivation_step_t        step,
                             mbedtls_svc_key_id_t             h_key);
psa_status_t c3tout_prf_import (const psa_key_attributes_t * p_attributes,
                                const uint8_t *              p_data,
                                size_t                       size,
                                mbedtls_svc_key_id_t *       ph_key);
void * c3tout_prf_alloc (size_t count, size_t size);

/* Allocation is bound into ssl_tls.c. Setup uses input and output buffers,
 * its scoped hostname setter uses one persistent name, and setup/renegotiation
 * initialize typed state records in the pinned SDK order. Certificate parsing
 * selects its temporary chain head and retained digest. Fixed leases have
 * no heap fallback on exhaustion. Other requests keep
 * the SDK allocator; this is not whole-TLS static-memory qualification.
 * psa_crypto.c uses the separate scoped PRF allocator and these exact-pointer
 * free/wipe functions, including aborts occurring outside the input call.
 */
void * c3tout_alloc (size_t count, size_t size);
void c3tout_free (void * p_memory);
void c3tout_wipe (void * p_memory, size_t size);
#endif

/* End of file. */

