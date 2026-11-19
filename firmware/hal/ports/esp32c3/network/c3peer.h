/**
 * @file c3peer.h
 * @brief Borrow the configuration view store for synchronous TLS chain nodes.
 */
#ifndef UTHOME_C3PEER_H
#define UTHOME_C3PEER_H
#include "cfgjson.h"

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

