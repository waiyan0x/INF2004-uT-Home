/**
 * @file c3ereg.h
 * @brief Fixed record leases for the SDK event loop and its registrations.
 */
#ifndef UTHOME_C3EREG_H
#define UTHOME_C3EREG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void * c3ereg_alloc (size_t count, size_t size, uint32_t caps);
void * c3ereg_body (size_t size, bool b_cleanup);
void c3ereg_free (void * p_memory);
#endif

/* End of file. */

