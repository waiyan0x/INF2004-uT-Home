/**
 * @file c3peer.h
 * @brief Borrow the configuration view store for synchronous TLS chain nodes.
 */
#ifndef UTHOME_C3PEER_H
#define UTHOME_C3PEER_H
#include "cfgjson.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "mbedtls/x509_crt.h"

typedef int c3peer_check_t (void *             p_context,
                            mbedtls_x509_crt * p_cert,
                            int                depth,
                            uint32_t *         p_flags);

int c3peer_verify (mbedtls_x509_crt *               p_chain,
                   mbedtls_x509_crt *               p_ca,
                   mbedtls_x509_crl *               p_crl,
                   const mbedtls_x509_crt_profile * p_profile,
                   const char *                     p_name,
                   uint32_t *                       p_flags,
                   c3peer_check_t *                 p_check,
                   void *                           p_context,
                   mbedtls_x509_crt_restart_ctx *   p_restart);
int c3peer_leaf (mbedtls_x509_crt * p_chain,
                 const uint8_t *    p_data,
                 size_t             size);
void * c3peer_key (size_t count, size_t size);
void c3peer_key_free (void * p_memory);
void * c3peer_limbs (size_t count, size_t size);
void c3peer_drop (void * p_memory);
void c3peer_wipe (void * p_memory, size_t size);
#endif

struct mbedtls_x509_crt;

/* USB and TLS run on one application task. Views live until close. The native
 * TLS parser releases every peer chain before returning to the owner loop. */
cfgjson_value_t * c3peer_views (void);
void c3peer_close (void);
int c3peer_der (struct mbedtls_x509_crt * p_chain,
                const unsigned char *     p_data,
                size_t                    size);
void * c3peer_alloc (size_t count, size_t size);
void c3peer_free (void * p_memory);
#endif

/* End of file. */

