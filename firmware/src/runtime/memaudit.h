/**
 * @file memaudit.h
 * @brief Fixed counters separating startup from normal-operation allocations.
 */
#ifndef UTHOME_MEMAUDIT_H
#define UTHOME_MEMAUDIT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UTHOME_AUDIT_KINDS      11U
#define UTHOME_AUDIT_TASK       0U
#define UTHOME_AUDIT_MUTEX      1U
#define UTHOME_AUDIT_SEM        2U
#define UTHOME_AUDIT_FLAG       3U
#define UTHOME_AUDIT_MAILBOX    4U
#define UTHOME_AUDIT_MESSAGE    5U
#define UTHOME_AUDIT_FIXED_POOL 6U
#define UTHOME_AUDIT_POOL       7U
#define UTHOME_AUDIT_PORT       8U
#define UTHOME_AUDIT_CYCLE      9U
#define UTHOME_AUDIT_ALARM      10U
#define UTHOME_AUDIT_ALLOCATORS 4U

typedef struct
{
    uint32_t allocations;
    uint32_t bytes;
    uint32_t frees;
    uint32_t creates[UTHOME_AUDIT_KINDS];
    uint32_t kernel_calls[UTHOME_AUDIT_ALLOCATORS];
} memaudit_phase_t;

typedef struct
{
    uint32_t allocations;
    uint32_t bytes;
    uint32_t frees;
    uint32_t kernel_calls[UTHOME_AUDIT_ALLOCATORS];
} memaudit_heap_t;

typedef struct
{
    memaudit_phase_t startup;
    memaudit_phase_t normal;
    memaudit_heap_t  application;
    bool             b_application;
    bool             b_sealed;
    bool             b_saturated;
} memaudit_t;

/* Zero-initialize once at reset, before any observed event. Callers serialize
 * all accesses. Seal is irreversible for the boot and never clears counters.
 * Creates count attempts, not live objects; heap counters count successful SDK
 * hook events. No allocation attempt, stack or whole-system claim is implied.
 */
int memaudit_seal (memaudit_t * p_state);
/* Mark entry to application startup without discarding earlier boot evidence.
 */
int memaudit_begin (memaudit_t * p_state);
int memaudit_create (memaudit_t * p_state, uint32_t kind);
int memaudit_heap (memaudit_t * p_state, size_t size, bool b_free);
/* Kernel allocator attempts: malloc, calloc, realloc, free, in that order. */
int memaudit_kernel (memaudit_t * p_state, uint32_t kind);

#endif

/* End of file. */

