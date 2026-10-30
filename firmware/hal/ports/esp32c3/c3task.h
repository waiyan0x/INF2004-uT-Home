/**
 * @file c3task.h
 * @brief Fixed-resource wireless task compatibility and observation interface.
 *
 * Task resources must be reserved during an approved startup phase.
 * No application heap allocation is permitted.
 */

#ifndef UTHOME_C3TASK_H
#define UTHOME_C3TASK_H

#include <stdbool.h>
#include <stdint.h>

#define C3TASK_OBSERVER_SLOTS      8U
#define C3TASK_OBSERVER_NAME_BYTES 16U

/** @brief Deferred shim cleanup callback. */
typedef int c3task_reap_t (void);

/**
 * @brief Start the permanent low-priority cleanup task.
 *
 * Static stack allocation is available before this call so SDK startup tasks
 * can be created before the project entry point. Initialization only starts
 * the surviving context used for deferred lifecycle cleanup.
 *
 * @return Zero on success or a negative value on failure.
 */
int c3task_init (void);

/** @brief Return the approved static task-slot capacity. */
uint32_t c3task_capacity (void);

/** @brief Return the number of occupied static task slots. */
uint32_t c3task_used (void);

/**
 * @brief Bind the patched shim's bounded cleanup operation.
 *
 * Rebinding to a different function is rejected. The callback is invoked only
 * by the permanent cleanup task, never from an ISR or a terminating task.
 *
 * @param p_reap Cleanup operation that reclaims at most one shim task.
 * @return Zero on success or a negative value on failure.
 */
int c3task_reaper_bind (c3task_reap_t * p_reap);

/**
 * @brief Reserve the smallest available fixed stack that fits a request.
 *
 * @param owner Nonzero generation-aware shim task token.
 * @param requested_bytes Requested ESP-IDF stack bytes.
 * @param pp_stack Destination for the aligned caller-owned stack.
 * @param p_capacity_bytes Destination for the physical stack capacity.
 * @return Zero on success or a negative value on invalid input/exhaustion.
 */
int c3task_stack_acquire (uintptr_t  owner,
                          uint32_t   requested_bytes,
                          void **    pp_stack,
                          uint32_t * p_capacity_bytes);

/**
 * @brief Release a stack after the corresponding kernel task is deleted.
 *
 * @param owner Generation-aware token used for acquisition.
 * @param p_stack Exact stack address returned by acquisition.
 * @param b_kernel_deleted True only after no kernel task can use the stack.
 * @return Zero on success or a negative value on stale/unsafe release.
 */
int c3task_stack_release (uintptr_t owner,
                          void *    p_stack,
                          bool      b_kernel_deleted);

/** @brief Aggregate task-observation statistics. */
typedef struct
{
    uint32_t requests;
    uint32_t successes;
    uint32_t failures;
    uint32_t deletions;
    uint32_t active;
    uint32_t peak_active;
    uint32_t maximum_requested_stack;
    uint32_t record_overflows;
} c3task_observer_stats_t;

/** @brief State of an observed task. */
typedef enum
{
    C3TASK_OBSERVER_FREE = 0,
    C3TASK_OBSERVER_PENDING,
    C3TASK_OBSERVER_ACTIVE
} c3task_observer_state_t;

/** @brief Snapshot of an individual observed task. */
typedef struct
{
    uintptr_t               handle;
    uint32_t                requested_stack_bytes;
    c3task_observer_state_t state;
    char                    name[C3TASK_OBSERVER_NAME_BYTES];
} c3task_observer_record_t;

/**
 * @brief Reset observation statistics before task activity begins.
 */
void c3task_observer_reset (void);

/**
 * @brief Begin observing a task-creation request.
 *
 * @param p_name Task name or NULL.
 * @param stack_bytes Requested stack size in bytes.
 * @return Nonzero token or zero when observation storage is full.
 */
uint32_t c3task_observer_begin (const char * p_name, uint32_t stack_bytes);

/**
 * @brief Associate an observation token with a created task.
 *
 * @param token Request token.
 * @param p_handle Task handle.
 * @return Zero on success or negative on failure.
 */
int c3task_observer_commit (uint32_t token, void * p_handle);

/**
 * @brief Abort a pending task-creation request.
 *
 * @param token Request token.
 * @return Zero on success or negative on failure.
 */
int c3task_observer_abort (uint32_t token);

/**
 * @brief Cancel a committed task after startup failure.
 *
 * @param p_handle Task handle.
 * @return Zero on success or negative on failure.
 */
int c3task_observer_cancel (void * p_handle);

/**
 * @brief Release a previously observed task.
 *
 * @param p_handle Task handle.
 * @return Zero on success or negative on failure.
 */
int c3task_observer_release (void * p_handle);

/**
 * @brief Read an individual observer record.
 *
 * @param index Observer record index.
 * @param p_out Destination for the record.
 * @return Zero on success or negative on failure.
 */
int c3task_observer_record (uint32_t index, c3task_observer_record_t * p_out);

/**
 * @brief Copy aggregate observation statistics.
 *
 * @param p_out Destination for the statistics.
 */
void c3task_observer_snapshot (c3task_observer_stats_t * p_out);

/* Legacy observer functions retained for existing host tests. */

void c3task_observer_request (const char * p_name, uint32_t stack_bytes);

void c3task_observer_created (void);

void c3task_observer_failed (void);

void c3task_observer_deleted (void);

#endif /* UTHOME_C3TASK_H */

/* End of file. */

