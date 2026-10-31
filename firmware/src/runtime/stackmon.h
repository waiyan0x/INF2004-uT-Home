/**
 * @file stackmon.h
 * @brief Bounded sentinel painting and unused-prefix stack observations.
 */
#ifndef UTHOME_STACKMON_H
#define UTHOME_STACKMON_H

#include <stddef.h>

#define STACKMON_MAX_BYTES 8448U

/* Paint before a task owns the storage, never while it can execute. Read only
 * when the stack owner is quiescent or appropriate single-core exclusion is
 * held. These stacks grow down from the end. Sentinel observations are
 * empirical high-water evidence, not a proof against all inputs or writes
 * matching 0xa5.
 */
int stackmon_paint (void * p_storage, size_t size);
int stackmon_unused (const void * p_storage, size_t size, size_t * p_unused);

#endif

/* End of file. */

