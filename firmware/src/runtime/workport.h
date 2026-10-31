/**
 * @file workport.h
 * @brief Declare the target periodic-worker and ownership contract.
 */
#ifndef UTHOME_WORKPORT_H
#define UTHOME_WORKPORT_H

#include "rt_port.h"
#include "workcost.h"

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
    uint32_t               source_index;
} rt_port_worker_t;

#define UTHOME_PORT_STOP_TIMEOUT_MS    500U
#define UTHOME_PORT_ERR_TIMEOUT        (-7)
#define UTHOME_PORT_ERR_CLEANUP        (-8)
#define UTHOME_PORT_ERR_BUSY           (-9)
#define UTHOME_PORT_WORKER_STACK_BYTES 8192U

typedef struct
{
    uint32_t unused_min;
    bool     b_measured;
} rt_port_stack_t;

/* Minimum untouched prefix observed since boot across all bindings. A worker
 * stack is scanned only after native dormancy; active stacks are never read.
 * The owner supplies space for exactly four records. Unmeasured is explicit. */
int rt_port_stack_capture (rt_port_stack_t * p_rows, size_t count);

typedef int rt_port_clock_fn_t (uint64_t * p_now_us);

typedef struct
{
    workcost_stats_t  stats;
    rt_port_task_id_t h_task;
    uint32_t          source_index;
    bool              b_bound;
    bool              b_available;
} rt_port_cost_t;

/* Install once before init. A missing clock leaves measurements unavailable.
 * The provider must be nonallocating, bounded and safe in ordinary task
 * context. Capture copies four slots under the existing shared guard. Totals
 * reset only when a dormant slot receives a new binding. Timings include
 * preemption. */
int rt_port_clock_set (rt_port_clock_fn_t * p_clock);
int rt_port_cost_capture (rt_port_cost_t * p_rows, size_t count);
/* Only an active worker contributes. Controller setup/discovery is excluded.
 * Invalid clocks or incomplete transfers poison this binding's measurements;
 * they never change the peripheral operation's result. */
void rt_port_cost_bus (uint64_t wait_us,
                       uint64_t active_us,
                       uint32_t bytes,
                       bool     b_timeout,
                       bool     b_error);
void rt_port_cost_invalid (void);

/* The controller alone changes the dispatch gate. Closing it prevents new
 * steps; an in-flight step must still finish before cleanup. Started workers
 * wait behind a closed gate, so partial startup cannot dispatch a component. */
int rt_port_gate_set (bool b_open);
bool rt_port_is_owner (void);
/* True only in the lifecycle owner's cleanup callback, outside shared guards.
 * HAL close operations may accept this context as well as ordinary ownership.
 * This does not permit lifecycle/query re-entry or opening new resources. */
bool rt_port_is_cleanup_owner (void);
int rt_port_owned_count (size_t * p_count);

/** @brief Perform one short, nonblocking queue or health operation. */
typedef int rt_port_shared_fn_t (void * p_context);

/* Task context only. On the kernel backend, another task's contention permits
 * three attempts separated by at most two one-ms delay requests, always before
 * taking the guard. Scheduler latency may extend those delays. Same-task
 * recursion returns ERR_BUSY immediately; exhausted contention also returns
 * ERR_BUSY for a later caller retry. The callback must not block or call
 * lifecycle APIs. No spin loop, allocation, driver I/O or ISR work belongs
 * inside the guard. The serial host backend does not simulate preemption. */
int rt_port_shared_call (rt_port_shared_fn_t * p_call, void * p_context);

/* Period must be 1..INT32_MAX ms; priority 5..19. Output changes on success.
 * Four permanent workers reserve 8 KiB each at boot. Admission still limits
 * requested stack budgets to 16 KiB total pending whole-system budgeting.
 * Requested stack sizes do not resize the permanent reservation.
 * No callback runs during prepare.
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
 * worker stays owned until stopped/cleaned; its kernel object is retained. */
int rt_port_task_result (rt_port_task_id_t h_task_id, int * p_result);

/* Target stop requests cancellation, wakes a sleeping worker, and waits up to
 * STOP_TIMEOUT_MS for dormancy. It never forcibly terminates a task. Cleanup
 * must succeed before binding ownership is released. Permanent TCBs remain.
 * Retry stop after a timeout/p_cleanup failure; retain the borrowed p_context.
 * The wait bound assumes callbacks yield or finish as contracted; it cannot
 * preempt an unbounded higher-priority callback. Cleanup has its own bound.
 * Target reconfigure only accepts a never-started dormant task and unchanged
 * stack size. Running updates require checked stop and a new dormant binding.
 * Whole-plan transactions intentionally remain unsupported. */

#endif

/* End of file. */

