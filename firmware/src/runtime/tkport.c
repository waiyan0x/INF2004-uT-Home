/**
 * @file tkport.c
 * @brief Own periodic tasks on the pinned single-core C3 kernel port.
 *
 * The controller retains every binding until the worker is dormant and cleanup
 * has succeeded. Boot-created tasks and owned stacks persist for the whole
 * boot. Atomic cancellation/result fields synchronize concurrent worker access;
 * volatile is not a substitute. Kernel declarations and callback casts preserve
 * the selected kernel ABI.
 */
#include "tkport.h"
#include "stackmon.h"
#include "workmeter.h"

#if defined(UTHOME_RUNTIME_UTKERNEL)
#include "uthome/runtime.h"
#include <tk/tkernel.h>
#include <stdatomic.h>

typedef struct
{
    rt_port_task_id_t     h_task_id;
    ID                    h_kernel_task;
    bool                  b_started;
    bool                  b_cleaned;
    rt_port_task_config_t config;
    rt_port_worker_t      worker;
    atomic_bool           b_cancel;
    atomic_int            result;
    rt_port_stack_t       stack;
    workmeter_t           meter;
    atomic_bool           b_cost_lost;
} worker_slot_t;

typedef struct
{
    worker_slot_t * p_slot;
    uint64_t        finished_us;
    uint64_t        lateness_us;
    bool            b_error;
} finish_cost_t;

static worker_slot_t g_slots[UTHOME_RUNTIME_MAX_DEVICE_TASKS] = {0};
/* uint64_t supplies the pinned kernel's required eight-byte stack alignment. */
static uint64_t g_stacks[UTHOME_RUNTIME_MAX_DEVICE_TASKS]
                        [UTHOME_PORT_WORKER_STACK_BYTES / sizeof(uint64_t)] = {
                            0};
static bool gb_boot_failed = false;
/* Rejected worker calls may inspect ownership while the controller cleans. */
static atomic_int           gh_owner       = 0;
static atomic_bool          gb_busy        = false;
static atomic_bool          gb_dispatch    = true;
static atomic_bool          gb_shared      = false;
static atomic_int           gh_shared_task = 0;
static uint32_t             gh_next_task   = 1U;
static rt_port_clock_fn_t * gp_clock       = NULL;

static ID task_context (void);
static bool is_owner (void);
static worker_slot_t * find_slot (rt_port_task_id_t h_task_id);
static bool valid_config (const rt_port_task_config_t * p_config);
static void worker_task (INT start_code, void * p_context);
static int await_release (worker_slot_t * p_slot, uint64_t * p_deadline);
static void measure_stack (worker_slot_t * p_slot);
static worker_slot_t * current_worker (void);
static int capture_cost (void * p_context);
static int finish_cost (void * p_context);
static void begin_cost (worker_slot_t * p_slot);
static void end_cost (worker_slot_t * p_slot, uint64_t deadline, int result);

/** @brief Fix the measurement clock before permanent workers exist. */
int rt_port_clock_set (rt_port_clock_fn_t * p_clock)
{
    if ((NULL == p_clock) || (NULL != gp_clock) || (0 != gh_owner) ||
        gb_boot_failed || (0 >= task_context()))
    {
        return -1;
    }
    gp_clock = p_clock;
    return 0;
}

/** @brief Copy a coherent bounded observation without exposing live storage. */
int rt_port_cost_capture (rt_port_cost_t * p_rows, size_t count)
{
    if ((!is_owner()) || (NULL == p_rows) ||
        (UTHOME_RUNTIME_MAX_DEVICE_TASKS != count))
    {
        return -1;
    }
    return rt_port_shared_call(capture_cost, p_rows);
}

/** @brief Record a transfer only in the calling worker's current step. */
void rt_port_cost_bus (uint64_t wait_us,
                       uint64_t active_us,
                       uint32_t bytes,
                       bool     b_timeout,
                       bool     b_error)
{
    worker_slot_t * p_slot = current_worker();
    if ((NULL != p_slot) && (NULL != gp_clock) &&
        (0 !=
         workmeter_bus(
             &p_slot->meter, wait_us, active_us, bytes, b_timeout, b_error)))
    {
        atomic_store(&p_slot->b_cost_lost, true);
    }
}

/** @brief Retain a missing measurement until the next dormant binding. */
void rt_port_cost_invalid (void)
{
    worker_slot_t * p_slot = current_worker();
    if (NULL != p_slot)
    {
        atomic_store(&p_slot->b_cost_lost, true);
    }
}

/** @brief Copy only dormant-worker observations, never live stack bytes. */
int rt_port_stack_capture (rt_port_stack_t * p_rows, size_t count)
{
    if ((!is_owner()) || (NULL == p_rows) ||
        (UTHOME_RUNTIME_MAX_DEVICE_TASKS != count))
    {
        return -1;
    }
    for (size_t index = 0U; index < count; ++index)
    {
        p_rows[index] = g_slots[index].stack;
    }
    return 0;
}

/** @brief Check lifecycle ownership without accessing controller-only data. */
bool rt_port_is_owner (void)
{
    return is_owner();
}

/** @brief Permit peripheral teardown only inside the owner's cleanup callback.
 */
bool rt_port_is_cleanup_owner (void)
{
    return (gh_owner > 0) && gb_busy && (!gb_shared) &&
           (task_context() == gh_owner);
}

/** @brief Count retained bindings, including failed cleanup.
 */
int rt_port_owned_count (size_t * p_count)
{
    if ((!is_owner()) || (NULL == p_count))
    {
        return -1;
    }
    size_t count = 0U;
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICE_TASKS; ++index)
    {
        if (0 != g_slots[index].h_task_id)
        {
            ++count;
        }
    }
    *p_count = count;
    return 0;
}

/** @brief Release prepared workers together or stop admitting new steps. */
int rt_port_gate_set (bool b_open)
{
    if (!is_owner())
    {
        return -1;
    }
    atomic_store(&gb_dispatch, b_open);
    return 0;
}

/** @brief Yield briefly to a preempted guard holder; never wait on ourselves.
 */
int rt_port_shared_call (rt_port_shared_fn_t * p_call, void * p_context)
{
    const ID h_caller = task_context();
    if ((NULL == p_call) || (0 >= gh_owner) || (0 >= h_caller))
    {
        return -1;
    }
    for (unsigned attempt = 0U; attempt < 3U; ++attempt)
    {
        if (!atomic_exchange(&gb_shared, true))
        {
            atomic_store(&gh_shared_task, h_caller);
            const int result = p_call(p_context);
            atomic_store(&gh_shared_task, 0);
            atomic_store(&gb_shared, false);
            return result;
        }
        if ((h_caller == atomic_load(&gh_shared_task)) || (2U == attempt))
        {
            return UTHOME_PORT_ERR_BUSY;
        }
        /* We own no guard here. Let a lower-priority holder finish instead
         * of losing a completed sample until the next full worker period.
         * Two one-ms delay requests bound retries, not scheduler latency. */
        if (E_OK != tk_dly_tsk(1U))
        {
            return -1;
        }
    }
    return UTHOME_PORT_ERR_BUSY;
}

/** @brief Read nonnegative, monotonic kernel operating time in task context. */
int rt_port_now_ms (uint64_t * p_now_ms)
{
    SYSTIM time;
    if ((NULL == p_now_ms) || (0 == task_context()) ||
        (E_OK != tk_get_otm(&time)) || (time.hi < 0))
    {
        return -1;
    }
    /* The checked, nonnegative high word fits uint32_t before widening.
     * Widening both unsigned words preserves all 64 operating-time bits. */
    *p_now_ms = ((uint64_t)(uint32_t)time.hi << 32U) | (uint64_t)time.lo;
    return 0;
}

/** @brief Reserve permanent workers once at boot, retaining failed startup.
 */
int rt_port_init (void)
{
    const ID h_caller = task_context();
    if ((h_caller <= 0) || gb_busy || gb_shared || gb_boot_failed ||
        ((0 != gh_owner) && (gh_owner != h_caller)))
    {
        return -1;
    }
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICE_TASKS; ++index)
    {
        if (0 != g_slots[index].h_task_id)
        {
            return -1;
        }
    }
    if (0 == gh_owner)
    {
        /* Startup is serialized. A partial failure is latched until reboot;
         * it cannot create further objects during normal operation. */
        for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
             ++index)
        {
            if (0 != stackmon_paint(g_stacks[index], sizeof(g_stacks[index])))
            {
                gb_boot_failed = true;
                return -1;
            }
            /* The pinned dispatcher uses INT, void * for its generic FP.
             * The fixed 8192-byte size fits SZ, with caller-owned alignment. */
            const uint32_t attributes    = TA_HLNG | TA_RNG0 | TA_USERBUF;
            const SZ       stack_size    = UTHOME_PORT_WORKER_STACK_BYTES;
            const T_CTSK   task          = {.exinf   = &g_slots[index],
                                            .tskatr  = attributes,
                                            .task    = (FP)worker_task,
                                            .itskpri = 19,
                                            .stksz   = stack_size,
                                            .bufptr  = g_stacks[index]};
            const ID       h_kernel_task = tk_cre_tsk(&task);
            if (h_kernel_task <= 0)
            {
                gb_boot_failed = true;
                return -1;
            }
            g_slots[index].h_kernel_task = h_kernel_task;
        }
    }
    gh_owner = h_caller;
    return 0;
}

/** @brief Bind an existing dormant task to exclusively borrowed context.
 */
int rt_port_task_prepare_worker (const rt_port_task_config_t * p_config,
                                 const rt_port_worker_t *      p_worker,
                                 rt_port_task_id_t *           ph_task_id)
{
    if ((!is_owner()) || (!valid_config(p_config)) || (NULL == ph_task_id) ||
        (NULL == p_worker) || (NULL == p_worker->p_step) ||
        (NULL == p_worker->p_cleanup) || (NULL == p_worker->p_context) ||
        (p_worker->source_index >= UTHOME_RUNTIME_MAX_DEVICES) ||
        (gh_next_task > UINT32_C(2147483647)))
    {
        return -1;
    }
    worker_slot_t * p_free_slot = NULL;
    size_t          stacks      = 0U;
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICE_TASKS; ++index)
    {
        worker_slot_t * const p_slot = &g_slots[index];
        if (0 == p_slot->h_task_id)
        {
            if (NULL == p_free_slot)
            {
                p_free_slot = p_slot;
            }
        }
        else
        {
            if (p_slot->worker.p_context == p_worker->p_context)
            {
                return -1;
            }
            stacks += p_slot->config.stack_size_bytes;
        }
    }
    if ((NULL == p_free_slot) || (stacks > (UTHOME_RUNTIME_DEVICE_STACK_BUDGET -
                                            p_config->stack_size_bytes)))
    {
        return -2;
    }
    T_RTSK state;
    if ((E_OK != tk_ref_tsk(p_free_slot->h_kernel_task, &state)) ||
        (TTS_DMT != state.tskstat) ||
        (E_OK != tk_chg_pri(p_free_slot->h_kernel_task, p_config->priority)))
    {
        return -1;
    }
    p_free_slot->config    = *p_config;
    p_free_slot->worker    = *p_worker;
    p_free_slot->b_started = false;
    p_free_slot->b_cleaned = false;
    atomic_init(&p_free_slot->b_cancel, false);
    atomic_init(&p_free_slot->result, 0);
    p_free_slot->meter = (workmeter_t){0};
    atomic_init(&p_free_slot->b_cost_lost, false);
    /* The bound above keeps the never-reused handle in positive int32_t. */
    p_free_slot->h_task_id = (rt_port_task_id_t)gh_next_task;
    ++gh_next_task;
    *ph_task_id = p_free_slot->h_task_id;
    return 0;
}

/** @brief Reject metadata-only task creation on the worker-requiring target. */
int rt_port_task_prepare (const rt_port_task_config_t * p_config,
                          rt_port_task_id_t *           ph_task_id)
{
    /* These arguments belong to the host-only compatibility entry point. */
    (void)p_config;
    (void)ph_task_id;
    return -1;
}

/** @brief Start a never-started, uncancelled task owned by the controller. */
int rt_port_task_start (rt_port_task_id_t h_task_id)
{
    if (!is_owner())
    {
        return -1;
    }
    worker_slot_t * const p_slot = find_slot(h_task_id);
    if ((NULL == p_slot) || (p_slot->b_started) ||
        (atomic_load(&p_slot->b_cancel)))
    {
        return -1;
    }
    /* Starting a higher-priority task may preempt the controller immediately.
     */
    p_slot->b_started = true;
    if (E_OK != tk_sta_tsk(p_slot->h_kernel_task, 0))
    {
        p_slot->b_started = false;
        return -1;
    }
    return 0;
}

/** @brief Cancel, await dormancy and release a binding, retaining failures.
 */
int rt_port_task_stop (rt_port_task_id_t h_task_id)
{
    if (!is_owner())
    {
        return -1;
    }
    worker_slot_t * const p_slot = find_slot(h_task_id);
    if (NULL == p_slot)
    {
        return -1;
    }
    uint64_t start = 0U;
    uint64_t now   = 0U;
    if (0 != rt_port_now_ms(&start))
    {
        return -1;
    }
    T_RTSK state;
    if (E_OK != tk_ref_tsk(p_slot->h_kernel_task, &state))
    {
        return -1;
    }
    atomic_store(&p_slot->b_cancel, true);
    if (TTS_DMT != state.tskstat)
    {
        const ER wake_result = tk_wup_tsk(p_slot->h_kernel_task);
        /* E_OBJ means dormant; E_QOVR means a wakeup was already queued. */
        if ((E_OK != wake_result) && (E_OBJ != wake_result) &&
            (E_QOVR != wake_result))
        {
            return -1;
        }
    }
    for (;;)
    {
        if (E_OK != tk_ref_tsk(p_slot->h_kernel_task, &state))
        {
            return -1;
        }
        if (TTS_DMT == state.tskstat)
        {
            break;
        }
        if (0 != rt_port_now_ms(&now))
        {
            return -1;
        }
        if ((now - start) >= UTHOME_PORT_STOP_TIMEOUT_MS)
        {
            return UTHOME_PORT_ERR_TIMEOUT;
        }
        if (E_OK != tk_dly_tsk(1U))
        {
            return -1;
        }
    }
    measure_stack(p_slot);
    if (!p_slot->b_cleaned)
    {
        /* Prevent controller-side cleanup callbacks from re-entering the API.
         */
        gb_busy          = true;
        const int result = p_slot->worker.p_cleanup(p_slot->worker.p_context);
        gb_busy          = false;
        if (0 != result)
        {
            return UTHOME_PORT_ERR_CLEANUP;
        }
        p_slot->b_cleaned = true;
    }
    /* The kernel object and its stack stay reserved; only the binding ends. */
    p_slot->h_task_id = 0;
    return 0;
}

/** @brief Update priority and period before first start, keeping stack size. */
int rt_port_task_reconfigure (rt_port_task_id_t             h_task_id,
                              const rt_port_task_config_t * p_config)
{
    if ((!is_owner()) || (!valid_config(p_config)))
    {
        return -1;
    }
    worker_slot_t * const p_slot = find_slot(h_task_id);
    if ((NULL == p_slot) || (p_slot->b_started) ||
        (atomic_load(&p_slot->b_cancel)) ||
        (p_config->stack_size_bytes != p_slot->config.stack_size_bytes))
    {
        return -1;
    }
    if (E_OK != tk_chg_pri(p_slot->h_kernel_task, p_config->priority))
    {
        return -1;
    }
    p_slot->config = *p_config;
    return 0;
}

/** @brief Report whether the controller still owns the supplied handle. */
int rt_port_task_is_prepared (rt_port_task_id_t h_task_id, bool * pb_prepared)
{
    if ((!is_owner()) || (h_task_id <= 0) || (NULL == pb_prepared))
    {
        return -1;
    }
    *pb_prepared = (NULL != find_slot(h_task_id));
    return 0;
}

/** @brief Report kernel nondormancy rather than historical start state. */
int rt_port_task_is_running (rt_port_task_id_t h_task_id, bool * pb_running)
{
    if ((!is_owner()) || (NULL == pb_running))
    {
        return -1;
    }
    const worker_slot_t * const p_slot = find_slot(h_task_id);
    T_RTSK                      state;
    if ((NULL == p_slot) || (E_OK != tk_ref_tsk(p_slot->h_kernel_task, &state)))
    {
        return -1;
    }
    *pb_running = (TTS_DMT != state.tskstat);
    return 0;
}

/** @brief Read the most recent step or wait result without releasing a task. */
int rt_port_task_result (rt_port_task_id_t h_task_id, int * p_result)
{
    if ((!is_owner()) || (NULL == p_result))
    {
        return -1;
    }
    const worker_slot_t * const p_slot = find_slot(h_task_id);
    if (NULL == p_slot)
    {
        return -1;
    }
    *p_result = atomic_load(&p_slot->result);
    return 0;
}

/** @brief Reject unimplemented whole-plan transactions before any mutation. */
int rt_port_transaction_begin (void)
{
    return -1;
}

/** @brief Reject unimplemented whole-plan transaction commits. */
int rt_port_transaction_commit (void)
{
    return -1;
}

/** @brief Reject unimplemented whole-plan transaction rollback. */
int rt_port_transaction_rollback (void)
{
    return -1;
}

#else

/** @brief Reject measurements without the selected native backend. */
int rt_port_clock_set (rt_port_clock_fn_t * p_clock)
{
    /* The unselected backend cannot run a measurement provider. */
    (void)p_clock;
    return -1;
}

/** @brief Keep missing native workload data explicitly unavailable. */
int rt_port_cost_capture (rt_port_cost_t * p_rows, size_t count)
{
    /* No native workers exist in this backend. */
    (void)p_rows;
    (void)count;
    return -1;
}

/** @brief Ignore observations when no native worker can own them. */
void rt_port_cost_bus (uint64_t wait_us,
                       uint64_t active_us,
                       uint32_t bytes,
                       bool     b_timeout,
                       bool     b_error)
{
    /* No native measurements exist in this deliberately unselected backend. */
    (void)wait_us;
    (void)active_us;
    (void)bytes;
    (void)b_timeout;
    (void)b_error;
}

/** @brief No binding exists to invalidate without native workers. */
void rt_port_cost_invalid (void)
{
    /* Measurements are already unavailable. */
}

/** @brief Leave stack evidence unavailable without the selected kernel. */
int rt_port_stack_capture (rt_port_stack_t * p_rows, size_t count)
{
    /* No native stacks exist for this deliberately unselected backend. */
    (void)p_rows;
    (void)count;
    return -1;
}

/* No kernel was selected, so preserve the native fail-closed guard.
 * Casts to void below mark intentionally unused, ABI-required arguments. */

/** @brief Reject controller checks without a selected kernel. */
bool rt_port_is_owner (void)
{
    return false;
}

/** @brief Reject cleanup ownership without a selected kernel. */
bool rt_port_is_cleanup_owner (void)
{
    return false;
}

/** @brief Reject resource queries without changing caller storage. */
int rt_port_owned_count (size_t * p_count)
{
    (void)p_count;
    return -1;
}

/** @brief Reject dispatch changes without a selected kernel. */
int rt_port_gate_set (bool b_open)
{
    (void)b_open;
    return -1;
}

/** @brief Never call a shared operation without a selected kernel. */
int rt_port_shared_call (rt_port_shared_fn_t * p_call, void * p_context)
{
    (void)p_call;
    (void)p_context;
    return -1;
}

/** @brief Reject initialization when no kernel backend is selected. */
int rt_port_init (void)
{
    return -1;
}

/** @brief Reject transactions when no kernel backend is selected. */
int rt_port_transaction_begin (void)
{
    return -1;
}

/** @brief Reject commits when no kernel backend is selected. */
int rt_port_transaction_commit (void)
{
    return -1;
}

/** @brief Reject rollback when no kernel backend is selected. */
int rt_port_transaction_rollback (void)
{
    return -1;
}

/** @brief Reject preparation without a kernel, leaving the output unchanged. */
int rt_port_task_prepare (const rt_port_task_config_t * p_config,
                          rt_port_task_id_t *           ph_task_id)
{
    (void)p_config;
    (void)ph_task_id;
    return -1;
}

/** @brief Reject task start when no kernel backend is selected. */
int rt_port_task_start (rt_port_task_id_t h_task_id)
{
    (void)h_task_id;
    return -1;
}

/** @brief Reject task stop when no kernel backend is selected. */
int rt_port_task_stop (rt_port_task_id_t h_task_id)
{
    (void)h_task_id;
    return -1;
}

/** @brief Reject reconfiguration when no kernel backend is selected. */
int rt_port_task_reconfigure (rt_port_task_id_t             h_task_id,
                              const rt_port_task_config_t * p_config)
{
    (void)h_task_id;
    (void)p_config;
    return -1;
}

/** @brief Report no prepared task and reject the query without a kernel. */
int rt_port_task_is_prepared (rt_port_task_id_t h_task_id, bool * pb_prepared)
{
    (void)h_task_id;
    if (NULL != pb_prepared)
    {
        *pb_prepared = false;
    }
    return -1;
}

/** @brief Report no running task and reject the query without a kernel. */
int rt_port_task_is_running (rt_port_task_id_t h_task_id, bool * pb_running)
{
    (void)h_task_id;
    if (NULL != pb_running)
    {
        *pb_running = false;
    }
    return -1;
}

/** @brief Reject worker preparation, leaving the output unchanged. */
int rt_port_task_prepare_worker (const rt_port_task_config_t * p_config,
                                 const rt_port_worker_t *      p_worker,
                                 rt_port_task_id_t *           ph_task_id)
{
    (void)p_config;
    (void)p_worker;
    (void)ph_task_id;
    return -1;
}

/** @brief Reject time queries without a kernel, leaving the output unchanged.
 */
int rt_port_now_ms (uint64_t * p_now_ms)
{
    (void)p_now_ms;
    return -1;
}

/** @brief Reject result queries without a kernel, leaving the output unchanged.
 */
int rt_port_task_result (rt_port_task_id_t h_task_id, int * p_result)
{
    (void)h_task_id;
    (void)p_result;
    return -1;
}

#endif /* UTHOME_RUNTIME_UTKERNEL */

#if defined(UTHOME_RUNTIME_UTKERNEL)

/** @brief Return the current task handle, or zero outside task context. */
static ID task_context (void)
{
    T_RSYS state;
    if ((E_OK != tk_ref_sys(&state)) || (TSS_TSK != state.sysstat))
    {
        return 0;
    }
    return state.runtskid;
}

/** @brief Check controller identity and reject callback re-entry. */
static bool is_owner (void)
{
    return (gh_owner > 0) && (!gb_busy) && (!gb_shared) &&
           (task_context() == gh_owner);
}

/** @brief Find an owned slot without admitting stale or invalid handles. */
static worker_slot_t * find_slot (rt_port_task_id_t h_task_id)
{
    if (h_task_id <= 0)
    {
        return NULL;
    }
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICE_TASKS; ++index)
    {
        if (g_slots[index].h_task_id == h_task_id)
        {
            return &g_slots[index];
        }
    }
    return NULL;
}

/** @brief Observe a dormant permanent stack before releasing its binding. */
static void measure_stack (worker_slot_t * p_slot)
{
    /* find_slot supplied an element of this fixed four-slot array. */
    const size_t index  = (size_t)(p_slot - g_slots);
    size_t       unused = 0U;
    if (0 != stackmon_unused(g_stacks[index], sizeof(g_stacks[index]), &unused))
    {
        p_slot->stack.b_measured = false;
        return;
    }
    if ((!p_slot->stack.b_measured) || (unused < p_slot->stack.unused_min))
    {
        /* The scanned array bounds the result to 8192 bytes. */
        p_slot->stack.unused_min = (uint32_t)unused;
    }
    p_slot->stack.b_measured = true;
}

/** @brief Validate the bounded priority, stack and relative timeout ranges. */
static bool valid_config (const rt_port_task_config_t * p_config)
{
    /* UINT32_C gives the positive, signed-kernel-timeout upper bound. */
    return (NULL != p_config) && (p_config->priority >= 5U) &&
           (p_config->priority <= 19U) &&
           (p_config->stack_size_bytes >= 1024U) &&
           (p_config->stack_size_bytes <= 8192U) &&
           (0U == (p_config->stack_size_bytes % 256U)) &&
           (p_config->period_ms > 0U) &&
           (p_config->period_ms <= UINT32_C(2147483647));
}

/** @brief Run finite periodic steps and remain owned after becoming dormant. */
static void worker_task (INT start_code, void * p_context)
{
    /* The kernel start argument is unused; exinf carries the owned slot. */
    (void)start_code;
    worker_slot_t * const p_slot   = p_context;
    uint64_t              deadline = 0U;
    uint64_t              now      = 0U;
    int                   result   = rt_port_now_ms(&deadline);
    while ((0 == result) && (!atomic_load(&p_slot->b_cancel)))
    {
        result = await_release(p_slot, &deadline);
        if ((0 != result) || (atomic_load(&p_slot->b_cancel)))
        {
            break;
        }
        begin_cost(p_slot);
        result = p_slot->worker.p_step(p_slot->worker.p_context);
        end_cost(p_slot, deadline, result);
        atomic_store(&p_slot->result, result);
        if ((0 != result) || (atomic_load(&p_slot->b_cancel)))
        {
            break;
        }
        result = rt_port_now_ms(&now);
        if (0 != result)
        {
            break;
        }
        deadline += p_slot->config.period_ms;
        if (deadline <= now)
        {
            deadline += (((now - deadline) / p_slot->config.period_ms) + 1U) *
                        p_slot->config.period_ms;
        }
        /* Queued wakeups prevent losing cancellation just before sleep. */
        while ((0 == result) && (!atomic_load(&p_slot->b_cancel)) &&
               (now < deadline))
        {
            /* The next future deadline is at most one validated period away;
             * that positive interval fits the kernel's signed 32-bit TMO. */
            const ER wait_result = tk_slp_tsk((TMO)(deadline - now));
            if ((E_OK != wait_result) && (E_TMOUT != wait_result))
            {
                /* ER and int are the kernel ABI's signed 32-bit status type. */
                result = (int)wait_result;
                break;
            }
            result = rt_port_now_ms(&now);
        }
    }
    atomic_store(&p_slot->result, result);
    /* Only the controller releases the dormant task's borrowed binding. */
    tk_ext_tsk();
}

/** @brief Yield behind the activation barrier and preserve cancellation. */
static int await_release (worker_slot_t * p_slot, uint64_t * p_deadline)
{
    bool b_waited = false;
    while ((!atomic_load(&gb_dispatch)) && (!atomic_load(&p_slot->b_cancel)))
    {
        const ER result = tk_slp_tsk(1);
        if ((E_OK != result) && (E_TMOUT != result))
        {
            /* Kernel ER is the selected ABI's signed int status type. */
            return (int)result;
        }
        b_waited = true;
    }
    if (b_waited)
    {
        return rt_port_now_ms(p_deadline);
    }
    return 0;
}

/** @brief Locate only immutable native IDs; never inspect another's meter. */
static worker_slot_t * current_worker (void)
{
    const ID h_caller = task_context();
    if (h_caller > 0)
    {
        for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
             ++index)
        {
            if (h_caller == g_slots[index].h_kernel_task)
            {
                return &g_slots[index];
            }
        }
    }
    return NULL;
}

/** @brief Copy published totals while their writer is excluded. */
static int capture_cost (void * p_context)
{
    rt_port_cost_t * p_rows = p_context;
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICE_TASKS; ++index)
    {
        const worker_slot_t * p_slot = &g_slots[index];
        p_rows[index]                = (rt_port_cost_t){
                           .stats        = p_slot->meter.stats,
                           .h_task       = p_slot->h_task_id,
                           .source_index = p_slot->worker.source_index,
                           .b_bound      = (0 != p_slot->h_task_id),
                           .b_available  = (NULL != gp_clock) &&
                           (!atomic_load(&p_slot->b_cost_lost)) &&
                           (0U != p_slot->meter.stats.steps)};
    }
    return 0;
}

/** @brief Start timing without changing application dispatch on failure. */
static void begin_cost (worker_slot_t * p_slot)
{
    uint64_t now_us = 0U;
    if ((NULL != gp_clock) && ((0 != gp_clock(&now_us)) ||
                               (0 != workmeter_begin(&p_slot->meter, now_us))))
    {
        atomic_store(&p_slot->b_cost_lost, true);
    }
}

/** @brief Include callback wall time, with separately quantized lateness. */
static void end_cost (worker_slot_t * p_slot, uint64_t deadline, int result)
{
    if (NULL == gp_clock)
    {
        return;
    }
    finish_cost_t  finish = {.p_slot = p_slot, .b_error = (0 != result)};
    uint64_t       now_ms = 0U;
    const uint64_t period = p_slot->config.period_ms;
    if ((0 != gp_clock(&finish.finished_us)) ||
        (0 != rt_port_now_ms(&now_ms)) || (deadline > (UINT64_MAX - period)))
    {
        atomic_store(&p_slot->b_cost_lost, true);
        return;
    }
    const uint64_t due     = deadline + period;
    const uint64_t late_ms = (now_ms > due) ? now_ms - due : 0U;
    if (late_ms > (UINT32_MAX / 1000U))
    {
        atomic_store(&p_slot->b_cost_lost, true);
        return;
    }
    finish.lateness_us = late_ms * 1000U;
    if (0 != rt_port_shared_call(finish_cost, &finish))
    {
        atomic_store(&p_slot->b_cost_lost, true);
    }
}

/** @brief Publish totals under the existing nonblocking shared guard. */
static int finish_cost (void * p_context)
{
    const finish_cost_t * p_finish = p_context;
    return workmeter_finish(&p_finish->p_slot->meter,
                            p_finish->finished_us,
                            p_finish->lateness_us,
                            p_finish->b_error);
}

#endif /* UTHOME_RUNTIME_UTKERNEL */

/* End of file. */

