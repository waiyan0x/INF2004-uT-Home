/**
 * @file c3umem.h
 * @brief Fixed startup-only storage for the pinned native UART records.
 */
#ifndef UTHOME_C3UMEM_H
#define UTHOME_C3UMEM_H
#include <stddef.h>
#define UTHOME_C3UMEM_BYTES 1280U

/* Called only by the two scoped native source substitutions during serial
 * registration, before application tasks run. No concurrent callers allowed.
 * Exact sizes only; duplicate/late requests return NULL with no heap fallback.
 * Free supports startup unwind only; unknown pointers and late frees do
 * nothing.
 */
void * c3umem_serial_alloc (size_t size);
void c3umem_serial_free (void * p_memory);
void * c3umem_record_alloc (size_t size);
void c3umem_record_free (void * p_memory);
/* Seal permanently; return zero only if both native records were provisioned.
 * Invoke before normal operation, even when no configured component uses UART.
 */
int c3umem_seal (void);
#endif
/* End of file. */

