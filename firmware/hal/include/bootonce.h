/**
 * @file bootonce.h
 * @brief Provision permanent resources once, retaining partial failures.
 */
#ifndef UTHOME_BOOTONCE_H
#define UTHOME_BOOTONCE_H

#include <stdbool.h>
#include <stddef.h>

typedef int bootonce_create_t (void * p_context, size_t index);

typedef struct
{
    bool b_attempted;
    bool b_ready;
} bootonce_t;

/* Zero-initialize state. Only the serialized startup owner may call run.
 * Create indices in order; a callback failure is final until reboot. Created
 * resources remain owned by the callback context. Nothing is retried/deleted.
 * Count must be positive; all pointers are borrowed through this call only. */
int bootonce_run (bootonce_t *        p_state,
                  size_t              count,
                  bootonce_create_t * p_create,
                  void *              p_context);
bool bootonce_ready (const bootonce_t * p_state);

#endif

/* End of file. */

