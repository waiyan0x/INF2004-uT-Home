/**
 * @file c3task.h
 * @brief Fixed-resource wireless task compatibility and observation interface.
 *
 * Task resources must be reserved during an approved startup phase.
 * No application heap allocation is permitted.
 */

#ifndef UTHOME_C3TASK_H
#define UTHOME_C3TASK_H

#include <stdint.h>

#define C3TASK_OBSERVER_SLOTS      8U
#define C3TASK_OBSERVER_NAME_BYTES 16U

/**
 * @brief Initialize fixed wireless task resources.
 *
 * @return Zero on success or a negative value on failure.
 */
int c3task_init (void);

/** @brief Return the approved static task-slot capacity. */
uint32_t c3task_capacity (void);

/** @brief Return the number of occupied static task slots. */
uint32_t c3task_used (void);

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

