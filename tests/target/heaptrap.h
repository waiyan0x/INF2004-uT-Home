/**
 * @file heaptrap.h
 * @brief Fail host tests when linked parsing paths reach a heap allocator.
 */
#ifndef UTHOME_HEAPTRAP_H
#define UTHOME_HEAPTRAP_H
#include <stddef.h>

void * heaptrap_allocate (size_t size);
void * heaptrap_zero (size_t count, size_t size);
void * heaptrap_resize (void * p_memory, size_t size);
void heaptrap_release (void * p_memory);
size_t heaptrap_count (void);

#endif

/* End of file. */

