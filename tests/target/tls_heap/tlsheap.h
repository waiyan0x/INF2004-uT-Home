/**
 * @file tlsheap.h
 * @brief Diagnostic link bindings for the ESP MQTT worker's TLS heap.
 */
#ifndef UTHOME_TLSHEAP_H
#define UTHOME_TLSHEAP_H

#include "esp_tls.h"
#include "mbedtls/ssl.h"
#include <stddef.h>

void * tlsheap_alloc (size_t count, size_t size);
void tlsheap_free (void * p_memory);
int tlsheap_step (mbedtls_ssl_context * p_ssl);
int tlsheap_connect (const char *          p_name,
                     int                   length,
                     int                   port,
                     const esp_tls_cfg_t * p_config,
                     esp_tls_t *           p_tls);

#endif

/* End of file. */

