/**
 * @file workport.h
 * @brief Declare the target periodic-worker and ownership contract.
 */
#ifndef UTHOME_WORKPORT_H
#define UTHOME_WORKPORT_H

#include "rt_port.h"

/** @brief Run one finite periodic step on the borrowed worker context. */
typedef int rt_port_step_fn_t (void * p_context);

/** @brief Release worker resources after the kernel task becomes dormant. */
typedef int rt_port_cleanup_fn_t (void * p_context);

/* Target periodic-worker contract. Link tkport.c with
 * UTHOME_RUNTIME_UTKERNEL defined and the pinned mtkernel component.
 * The task calling init owns ALL lifecycle/query calls. Interrupts, other
 * tasks and callback re-entry are rejected. The owner must stay alive.
 * p_step runs in the worker; p_cleanup runs in the owner after kernel dormancy.
 * Both callbacks must finish in bounded time. A failed p_cleanup must be safe
 * to retry. Context and callbacks remain borrowed until stop returns zero.
 * This interface does not activate the configuration-driven runtime yet. */
typedef struct
{
    rt_port_step_fn_t *    p_step;
    rt_port_cleanup_fn_t * p_cleanup;
    void *                 p_context;
} rt_port_worker_t;

#define UTHOME_PORT_STOP_TIMEOUT_MS 500U
#define UTHOME_PORT_ERR_TIMEOUT     (-7)
#define UTHOME_PORT_ERR_CLEANUP     (-8)
#define UTHOME_PORT_ERR_BUSY        (-9)

/* The controller alone changes the dispatch gate. Closing it prevents new
 * steps; an in-flight step must still finish before cleanup. Started workers
 * wait behind a closed gate, so partial startup cannot dispatch a component. */
int rt_port_gate_set (bool b_open);
bool rt_port_is_owner (void);
int rt_port_owned_count (size_t * p_count);

/** @brief Perform one short, nonblocking queue or health operation. */
typedef int rt_port_shared_fn_t (void * p_context);

/* Task context only. A competing or recursive call returns ERR_BUSY without
 * waiting; callers retry later. The callback must not block or call lifecycle
 * APIs. No spin loop, allocation, driver I/O or ISR work belongs here. */
int rt_port_shared_call (rt_port_shared_fn_t * p_call, void * p_context);

/* Period must be 1..INT32_MAX ms; priority 5..19. Output changes on success.
 * At most four workers and 16 KiB of stacks. No callback runs during prepare.
 * Handles are opaque and never reused during a boot, including after init.
 * A worker starts immediately, skips missed periods and exits on p_step error.
 * Streaming/readiness dispatch is a later integration step. */
int rt_port_task_prepare_worker (const rt_port_task_config_t * p_config,
                                 const rt_port_worker_t *      p_worker,
                                 rt_port_task_id_t *           ph_task_id);

/* Monotonic kernel operating time, in milliseconds; usable in task callbacks.
 * This is not the ESP-IDF startup logging timestamp. */
int rt_port_now_ms (uint64_t * p_now_ms);

/* Most recent p_step/wait error (zero before work or on success). A failing
 * worker stays owned and must still be stopped/cleaned/deleted explicitly. */
int rt_port_task_result (rt_port_task_id_t h_task_id, int * p_result);

/* Target stop requests cancellation, wakes a sleeping worker, and waits up to
 * STOP_TIMEOUT_MS for dormancy. It never forcibly terminates a task. Cleanup
 * and tk_del_tsk must both succeed before ownership is released. Retry stop
 * after a timeout/p_cleanup/delete failure; do not free the borrowed p_context.
 * The wait bound assumes callbacks yield or finish as contracted; it cannot
 * preempt an unbounded higher-priority callback. Cleanup has its own bound.
 * Target reconfigure only accepts a never-started dormant task and unchanged
 * stack size. Running updates/stack changes require checked stop + recreation.
 * Whole-plan transactions intentionally remain unsupported. */

#endif

/* End of file. */

