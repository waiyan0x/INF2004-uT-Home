/**
 * @file c3tls.h
 * @brief Fixed ESP-TLS connection records for the serialized network owner.
 */
#ifndef UTHOME_C3TLS_H
#define UTHOME_C3TLS_H

#include <stddef.h>
#include <stdint.h>
#include "esp_tls.h"

/* Selected ESP-TLS units alone use these allocator bindings. Three typed
 * contexts and one temporary, 253-character hostname have no heap fallback.
 * Only calloc(1, sizeof(esp_tls_t)) is supported. Calls run in task context
 * with cache enabled and one serialized owner; these are not general
 * allocators. A handle expires on release and must never be used after its slot
 * is reused.
 */
void * c3tls_alloc (size_t count, size_t size);
void c3tls_free (void * p_memory);
char * c3tls_dup (const char * p_text, size_t size);

/* SDK-compatible error tracker: independent fixed records, preserving the
 * public last-error fields and get-and-clear semantics. No native objects.
 */
esp_tls_error_handle_t c3tls_error_create (void);
void c3tls_error_destroy (esp_tls_error_handle_t h_error);
void c3tls_error_capture (esp_tls_error_handle_t h_error,
                          uint32_t               type,
                          int                    code);
esp_err_t c3tls_error_get (esp_tls_error_handle_t h_error,
                           esp_tls_error_type_t   type,
                           int *                  p_code);

#endif

/* End of file. */

