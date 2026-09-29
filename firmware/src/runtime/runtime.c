/**
 * @file runtime.c
 * @brief Admit and manage serialized component lifecycle and metrics.
 *
 * Port transactions cover task metadata only. Driver I/O occurs through bound
 * steps, never during apply. The controller serializes every public call.
 */
#include "uthome/runtime.h"
#include "rt_port.h"
#include <string.h>

typedef struct
{
    runtime_device_t  device;
    runtime_status_t  status;
    rt_port_task_id_t h_port;
    uint32_t          elapsed_ms;
    uint32_t          last_execution_ms;
    bool              b_has_execution;
    bool              b_bound;
    bool              b_has_rollback;
    bool              b_rollback_prepared;
    runtime_device_t  rollback_device;
    runtime_state_t   rollback_state;
    runtime_binding_t binding;
} runtime_entry_t;

/** @brief Apply a lifecycle operation to one owned runtime entry. */
typedef int runtime_lifecycle_fn_t (runtime_entry_t * p_entry);

typedef struct
{
    runtime_entry_t entries[UTHOME_RUNTIME_MAX_DEVICES];
    size_t          count;
    uint32_t        clock_ms;
} runtime_storage_t;

static runtime_storage_t g_state        = {0};
static runtime_storage_t g_saved        = {0};
static bool              gb_initialized = false;
static bool              gb_faulted     = false;
static bool              gb_busy        = false;

static void clear_state (runtime_storage_t * p_state);
static int find_device (const char * p_name);
static bool valid_device (const runtime_device_t * p_device);
static bool valid_binding (const runtime_binding_t * p_binding,
                           const char *              p_name);
static int writable (void);
static int begin (void);
static int finish (int result);
static bool stack_fits (const runtime_entry_t * p_exclude, size_t requested);
static rt_port_task_config_t port_config (const runtime_device_t * p_device);
static int prepare (runtime_entry_t * p_entry);
static int start (runtime_entry_t * p_entry);
static int stop (runtime_entry_t * p_entry);
static int update (runtime_entry_t *        p_entry,
                   const runtime_device_t * p_device);
static int lifecycle (const char *             p_name,
                      runtime_lifecycle_fn_t * p_operation);
static int validate_plan (const runtime_device_t *  p_devices,
                          const runtime_binding_t * p_bindings,
                          size_t                    count);
static int apply (const runtime_device_t *  p_devices,
                  const runtime_binding_t * p_bindings,
                  size_t                    count);
static void record_execution (runtime_entry_t * p_entry, uint32_t now_ms);
static int dispatch (runtime_entry_t * p_entry);
static int tick_entry (runtime_entry_t * p_entry, uint32_t elapsed_ms);

/** @brief Initialize the backend before admitting lifecycle operations. */
int runtime_init (void)
{
    if (gb_busy)
    {
        return UTHOME_RUNTIME_ERR_BUSY;
    }
    gb_initialized = false;
    gb_faulted     = true;
    if (0 != rt_port_init())
    {
        return UTHOME_RUNTIME_ERR_BACKEND;
    }
    clear_state(&g_state);
    clear_state(&g_saved);
    gb_initialized = true;
    gb_faulted     = false;
    return 0;
}

/** @brief Register one validated device without creating a task. */
int runtime_register_device (const runtime_device_t * p_device)
{
    int result = writable();
    if (0 != result)
    {
        return result;
    }
    if (!valid_device(p_device))
    {
        return -1;
    }
    if (g_state.count >= UTHOME_RUNTIME_MAX_DEVICES)
    {
        return -2;
    }
    if (find_device(p_device->name) >= 0)
    {
        return -3;
    }
    runtime_entry_t * const p_entry = &g_state.entries[g_state.count++];
    p_entry->device                 = *p_device;
    p_entry->h_port                 = -1;
    p_entry->status.state =
        p_device->b_enabled ? UTHOME_RUNTIME_READY : UTHOME_RUNTIME_STOPPED;
    return 0;
}

/** @brief Prepare one registered device transactionally. */
int runtime_prepare_device (const char * p_name)
{
    return lifecycle(p_name, prepare);
}

/** @brief Start one registered device transactionally. */
int runtime_start_device (const char * p_name)
{
    return lifecycle(p_name, start);
}

/** @brief Stop one registered device transactionally. */
int runtime_stop_device (const char * p_name)
{
    return lifecycle(p_name, stop);
}

/** @brief Save rollback settings and transactionally update a device. */
int runtime_update_device (const char *             p_name,
                           const runtime_device_t * p_device)
{
    const int index = find_device(p_name);
    if ((index < 0) || (!valid_device(p_device)) ||
        (0 != strcmp(p_name, p_device->name)))
    {
        return -1;
    }
    int result = begin();
    if (0 != result)
    {
        return result;
    }
    runtime_entry_t * const p_entry = &g_state.entries[index];
    p_entry->rollback_device        = p_entry->device;
    p_entry->rollback_state         = p_entry->status.state;
    p_entry->b_rollback_prepared    = p_entry->h_port >= 0;
    p_entry->b_has_rollback         = true;
    return finish(update(p_entry, p_device));
}

/** @brief Apply a device update through the same rollback contract. */
int runtime_reconfigure_device (const char *             p_name,
                                const runtime_device_t * p_device)
{
    return runtime_update_device(p_name, p_device);
}

/** @brief Restore the last saved configuration and lifecycle state. */
int runtime_rollback_device (const char * p_name)
{
    const int index = find_device(p_name);
    if (index < 0)
    {
        return -1;
    }
    if (!g_state.entries[index].b_has_rollback)
    {
        return -2;
    }
    int result = begin();
    if (0 != result)
    {
        return result;
    }
    runtime_entry_t * const p_entry    = &g_state.entries[index];
    const runtime_device_t  previous   = p_entry->rollback_device;
    const runtime_state_t   state      = p_entry->rollback_state;
    const bool              b_prepared = p_entry->b_rollback_prepared;
    result                             = stop(p_entry);
    if (0 == result)
    {
        p_entry->device = previous;
        if (b_prepared)
        {
            result = prepare(p_entry);
        }
        if ((0 == result) && (UTHOME_RUNTIME_RUNNING == state))
        {
            result = start(p_entry);
        }
        if (0 == result)
        {
            p_entry->status.state   = state;
            p_entry->b_has_rollback = false;
        }
    }
    return finish(result);
}

/** @brief Record a device failure and mark its runtime state erroneous. */
int runtime_record_failure (const char * p_name)
{
    int result = writable();
    if (0 != result)
    {
        return result;
    }
    const int index = find_device(p_name);
    if (index < 0)
    {
        return -1;
    }
    ++g_state.entries[index].status.failure_count;
    g_state.entries[index].status.state = UTHOME_RUNTIME_ERROR;
    return 0;
}

/** @brief Stop and start one device within a checked transaction. */
int runtime_restart_device (const char * p_name)
{
    const int index = find_device(p_name);
    if (index < 0)
    {
        return -1;
    }
    if (!g_state.entries[index].device.b_enabled)
    {
        return -2;
    }
    int result = begin();
    if (0 != result)
    {
        return result;
    }
    runtime_entry_t * const p_entry = &g_state.entries[index];
    result                          = stop(p_entry);
    if (0 == result)
    {
        result = start(p_entry);
    }
    if (0 == result)
    {
        ++p_entry->status.restart_count;
    }
    return finish(result);
}

/** @brief Apply host metadata without binding driver callbacks. */
int runtime_apply_devices (const runtime_device_t * p_devices, size_t count)
{
    return apply(p_devices, NULL, count);
}

/** @brief Apply one caller-owned binding per configured device. */
int runtime_apply_bound_devices (const runtime_device_t *  p_devices,
                                 const runtime_binding_t * p_bindings,
                                 size_t                    count)
{
    if ((0U != count) && (NULL == p_bindings))
    {
        return -1;
    }
    return apply(p_devices, p_bindings, count);
}

/** @brief Bind or unbind an unprepared device without sharing context. */
int runtime_bind_device (const char *              p_name,
                         const runtime_binding_t * p_binding)
{
    int result = writable();
    if (0 != result)
    {
        return result;
    }
    const int index = find_device(p_name);
    if (index < 0)
    {
        return -1;
    }
    runtime_entry_t * const p_entry = &g_state.entries[index];
    if (p_entry->h_port >= 0)
    {
        return -2;
    }
    if (NULL != p_binding)
    {
        if (!valid_binding(p_binding, p_name))
        {
            return -1;
        }
        /* The successful lookup makes index nonnegative and at most seven. */
        const size_t selected_index = (size_t)index;
        for (size_t other_index = 0U; other_index < g_state.count;
             ++other_index)
        {
            if ((other_index != selected_index) &&
                (g_state.entries[other_index].b_bound) &&
                (g_state.entries[other_index].binding.component.p_context ==
                 p_binding->component.p_context))
            {
                return -1;
            }
        }
        p_entry->binding = *p_binding;
    }
    else
    {
        memset(&p_entry->binding, 0, sizeof(p_entry->binding));
    }
    p_entry->b_bound = NULL != p_binding;
    return 0;
}

/** @brief Dispatch one running device under the re-entry guard. */
int runtime_dispatch_device (const char * p_name)
{
    int result = writable();
    if (0 != result)
    {
        return result;
    }
    const int index = find_device(p_name);
    if (index < 0)
    {
        return -1;
    }
    runtime_entry_t * const p_entry = &g_state.entries[index];
    if (UTHOME_RUNTIME_RUNNING != p_entry->status.state)
    {
        return -2;
    }
    gb_busy = true;
    result  = dispatch(p_entry);
    gb_busy = false;
    return result;
}

/** @brief Advance the host clock and service due running devices. */
int runtime_tick (uint32_t elapsed_ms)
{
    int result = writable();
    if (0 != result)
    {
        return result;
    }
    gb_busy = true;
    g_state.clock_ms += elapsed_ms;
    for (size_t index = 0U; index < g_state.count; ++index)
    {
        if (0 != tick_entry(&g_state.entries[index], elapsed_ms))
        {
            result = UTHOME_RUNTIME_ERR_BACKEND;
        }
    }
    gb_busy = false;
    return result;
}

/** @brief Record execution timing for one running device. */
int runtime_record_execution (const char * p_name, uint32_t now_ms)
{
    int result = writable();
    if (0 != result)
    {
        return result;
    }
    const int index = find_device(p_name);
    if (index < 0)
    {
        return -1;
    }
    if (UTHOME_RUNTIME_RUNNING != g_state.entries[index].status.state)
    {
        return -2;
    }
    record_execution(&g_state.entries[index], now_ms);
    return 0;
}

/** @brief Copy status for a registered device into caller storage. */
int runtime_get_status (const char * p_name, runtime_status_t * p_status)
{
    const int index = find_device(p_name);
    if ((index < 0) || (NULL == p_status))
    {
        return -1;
    }
    *p_status = g_state.entries[index].status;
    return 0;
}

/** @brief Copy device settings into caller storage. */
int runtime_get_device (const char * p_name, runtime_device_t * p_device)
{
    const int index = find_device(p_name);
    if ((index < 0) || (NULL == p_device))
    {
        return -1;
    }
    *p_device = g_state.entries[index].device;
    return 0;
}

/** @brief Report reserved tasks and stacks, retaining recovery status. */
int runtime_get_resources (runtime_resource_snapshot_t * p_snapshot)
{
    if ((NULL == p_snapshot) || (!gb_initialized))
    {
        return -1;
    }
    *p_snapshot = (runtime_resource_snapshot_t){
        .registered_devices = g_state.count,
        .max_tasks          = UTHOME_RUNTIME_MAX_DEVICE_TASKS};
    for (size_t index = 0U; index < g_state.count; ++index)
    {
        if (g_state.entries[index].h_port >= 0)
        {
            ++p_snapshot->active_tasks;
            p_snapshot->configured_stack_bytes +=
                g_state.entries[index].device.stack_size_bytes;
        }
    }
    p_snapshot->free_task_slots =
        p_snapshot->max_tasks - p_snapshot->active_tasks;
    return gb_faulted ? UTHOME_RUNTIME_ERR_RECOVERY : 0;
}

/** @brief Report whether uncertain backend ownership blocks operations. */
bool runtime_is_faulted (void)
{
    return gb_faulted;
}

/** @brief Return the number of registered devices. */
size_t runtime_device_count (void)
{
    return g_state.count;
}

/** @brief Clear metadata and mark all task handles unprepared. */
static void clear_state (runtime_storage_t * p_state)
{
    memset(p_state, 0, sizeof(*p_state));
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICES; ++index)
    {
        p_state->entries[index].h_port = -1;
    }
}

/** @brief Find a registered name, returning a negative status if absent. */
static int find_device (const char * p_name)
{
    if (NULL == p_name)
    {
        return -1;
    }
    for (size_t index = 0U; index < g_state.count; ++index)
    {
        if (0 == strcmp(g_state.entries[index].device.name, p_name))
        {
            /* The fixed eight-entry capacity fits every conforming C int. */
            return (int)index;
        }
    }
    return -1;
}

/** @brief Validate a terminated name and bounded priority and stack size. */
static bool valid_device (const runtime_device_t * p_device)
{
    return (NULL != p_device) && ('\0' != p_device->name[0]) &&
           (NULL != memchr(p_device->name, '\0', sizeof(p_device->name))) &&
           (p_device->priority >= 1U) && (p_device->priority <= 31U) &&
           (p_device->stack_size_bytes >= 1024U) &&
           (p_device->stack_size_bytes <= 8192U) &&
           (0U == (p_device->stack_size_bytes % 256U));
}

/** @brief Require a step callback and matching component identity. */
static bool valid_binding (const runtime_binding_t * p_binding,
                           const char *              p_name)
{
    return (NULL != p_binding) && (NULL != p_binding->p_step) &&
           (NULL != p_binding->component.p_context) &&
           (NULL != p_binding->component.p_operations) &&
           (NULL != memchr(p_binding->component.name,
                           '\0',
                           sizeof(p_binding->component.name))) &&
           (0 == strcmp(p_binding->component.name, p_name));
}

/** @brief Reject calls while busy, uninitialized or awaiting recovery. */
static int writable (void)
{
    if (gb_busy)
    {
        return UTHOME_RUNTIME_ERR_BUSY;
    }
    if (!gb_initialized)
    {
        return UTHOME_RUNTIME_ERR_BACKEND;
    }
    return gb_faulted ? UTHOME_RUNTIME_ERR_RECOVERY : 0;
}

/** @brief Checkpoint host metadata before any lifecycle mutation. */
static int begin (void)
{
    int result = writable();
    if (0 != result)
    {
        return result;
    }
    gb_busy = true;
    if (0 != rt_port_transaction_begin())
    {
        gb_busy = false;
        return UTHOME_RUNTIME_ERR_BACKEND;
    }
    g_saved = g_state;
    return 0;
}

/** @brief Commit success or restore the checkpoint after a checked rollback. */
static int finish (int result)
{
    if ((0 == result) && (0 == rt_port_transaction_commit()))
    {
        gb_busy = false;
        return 0;
    }
    if (0 == result)
    {
        result = UTHOME_RUNTIME_ERR_BACKEND;
    }
    if (0 != rt_port_transaction_rollback())
    {
        /* Ownership is uncertain: preserve handles, prohibit reuse/dispatch. */
        gb_faulted = true;
        for (size_t index = 0U; index < g_state.count; ++index)
        {
            g_state.entries[index].status.state = UTHOME_RUNTIME_ERROR;
        }
        gb_busy = false;
        return UTHOME_RUNTIME_ERR_RECOVERY;
    }
    g_state = g_saved;
    gb_busy = false;
    return result;
}

/** @brief Reserve stack capacity while excluding a replaced task. */
static bool stack_fits (const runtime_entry_t * p_exclude, size_t requested)
{
    size_t used = 0U;
    for (size_t index = 0U; index < g_state.count; ++index)
    {
        const runtime_entry_t * const p_entry = &g_state.entries[index];
        if ((p_entry != p_exclude) && (p_entry->h_port >= 0))
        {
            used += p_entry->device.stack_size_bytes;
        }
    }
    return (requested <= UTHOME_RUNTIME_DEVICE_STACK_BUDGET) &&
           (used <= (UTHOME_RUNTIME_DEVICE_STACK_BUDGET - requested));
}

/** @brief Copy scheduling settings into the backend task contract. */
static rt_port_task_config_t port_config (const runtime_device_t * p_device)
{
    return (rt_port_task_config_t){.priority = p_device->priority,
                                   .stack_size_bytes =
                                       p_device->stack_size_bytes,
                                   .period_ms = p_device->period_ms};
}

/** @brief Reserve a task resource without starting device execution. */
static int prepare (runtime_entry_t * p_entry)
{
    if (!p_entry->device.b_enabled)
    {
        return -2;
    }
    if (p_entry->h_port >= 0)
    {
        return 0;
    }
    if (!stack_fits(p_entry, p_entry->device.stack_size_bytes))
    {
        return -3;
    }
    const rt_port_task_config_t config = port_config(&p_entry->device);
    rt_port_task_id_t           h_task = -1;
    if (0 != rt_port_task_prepare(&config, &h_task))
    {
        return -3;
    }
    p_entry->h_port       = h_task;
    p_entry->status.state = UTHOME_RUNTIME_READY;
    return 0;
}

/** @brief Prepare if needed, then start the associated backend task. */
static int start (runtime_entry_t * p_entry)
{
    if (!p_entry->device.b_enabled)
    {
        return -2;
    }
    if (UTHOME_RUNTIME_RUNNING == p_entry->status.state)
    {
        return 0;
    }
    int result = prepare(p_entry);
    if (0 != result)
    {
        return result;
    }
    if (0 != rt_port_task_start(p_entry->h_port))
    {
        return UTHOME_RUNTIME_ERR_BACKEND;
    }
    p_entry->status.state = UTHOME_RUNTIME_RUNNING;
    return 0;
}

/** @brief Release a backend task before clearing its retained handle. */
static int stop (runtime_entry_t * p_entry)
{
    if ((p_entry->h_port >= 0) && (0 != rt_port_task_stop(p_entry->h_port)))
    {
        return UTHOME_RUNTIME_ERR_BACKEND;
    }
    p_entry->h_port          = -1;
    p_entry->status.state    = UTHOME_RUNTIME_STOPPED;
    p_entry->elapsed_ms      = 0U;
    p_entry->b_has_execution = false;
    return 0;
}

/** @brief Apply settings while preserving prior prepared and running state. */
static int update (runtime_entry_t * p_entry, const runtime_device_t * p_device)
{
    const bool b_prepared = p_entry->h_port >= 0;
    const bool b_running  = UTHOME_RUNTIME_RUNNING == p_entry->status.state;
    if (b_prepared && (p_device->b_enabled) &&
        (!stack_fits(p_entry, p_device->stack_size_bytes)))
    {
        return -2;
    }
    if ((!p_device->b_enabled) ||
        (b_prepared &&
         (p_device->stack_size_bytes != p_entry->device.stack_size_bytes)))
    {
        int result = stop(p_entry);
        if (0 != result)
        {
            return result;
        }
        p_entry->device = *p_device;
        if (!p_device->b_enabled)
        {
            return 0;
        }
        result = b_prepared ? prepare(p_entry) : 0;
        if (0 != result)
        {
            return result;
        }
        return b_running ? start(p_entry) : 0;
    }
    if (b_prepared)
    {
        const rt_port_task_config_t config = port_config(p_device);
        if (0 != rt_port_task_reconfigure(p_entry->h_port, &config))
        {
            return UTHOME_RUNTIME_ERR_BACKEND;
        }
    }
    p_entry->device          = *p_device;
    p_entry->elapsed_ms      = 0U;
    p_entry->b_has_execution = false;
    if (!b_prepared)
    {
        p_entry->status.state = UTHOME_RUNTIME_READY;
    }
    return 0;
}

/** @brief Run one named lifecycle operation within a checked transaction. */
static int lifecycle (const char * p_name, runtime_lifecycle_fn_t * p_operation)
{
    const int index = find_device(p_name);
    if (index < 0)
    {
        return -1;
    }
    int result = begin();
    return (0 != result) ? result
                         : finish(p_operation(&g_state.entries[index]));
}

/** @brief Check each device, context uniqueness and aggregate resources. */
static int validate_plan (const runtime_device_t *  p_devices,
                          const runtime_binding_t * p_bindings,
                          size_t                    count)
{
    size_t active = 0U;
    size_t stacks = 0U;
    for (size_t index = 0U; index < count; ++index)
    {
        if (!valid_device(&p_devices[index]))
        {
            return -1;
        }
        if ((NULL != p_bindings) &&
            (!valid_binding(&p_bindings[index], p_devices[index].name)))
        {
            return -1;
        }
        for (size_t previous_index = 0U; previous_index < index;
             ++previous_index)
        {
            if (0 ==
                strcmp(p_devices[index].name, p_devices[previous_index].name))
            {
                return -3;
            }
            if ((NULL != p_bindings) &&
                (p_bindings[index].component.p_context ==
                 p_bindings[previous_index].component.p_context))
            {
                return -1;
            }
        }
        if (p_devices[index].b_enabled)
        {
            ++active;
            stacks += p_devices[index].stack_size_bytes;
        }
    }
    if ((active > UTHOME_RUNTIME_MAX_DEVICE_TASKS) ||
        (stacks > UTHOME_RUNTIME_DEVICE_STACK_BUDGET))
    {
        return -2;
    }
    return 0;
}

/** @brief Replace the full plan only after validation and checked task
 * teardown. */
static int apply (const runtime_device_t *  p_devices,
                  const runtime_binding_t * p_bindings,
                  size_t                    count)
{
    int result = writable();
    if (0 != result)
    {
        return result;
    }
    if (count > UTHOME_RUNTIME_MAX_DEVICES)
    {
        return -2;
    }
    if ((0U != count) && (NULL == p_devices))
    {
        return -1;
    }
    result = validate_plan(p_devices, p_bindings, count);
    if (0 != result)
    {
        return result;
    }
    /* Copy inputs first: callers may be reapplying entries copied from a
     * binding. */
    runtime_storage_t candidate;
    clear_state(&candidate);
    candidate.count    = count;
    candidate.clock_ms = g_state.clock_ms;
    for (size_t index = 0U; index < count; ++index)
    {
        candidate.entries[index].device = p_devices[index];
        if (NULL != p_bindings)
        {
            candidate.entries[index].binding = p_bindings[index];
            candidate.entries[index].b_bound = true;
        }
    }
    result = begin();
    if (0 != result)
    {
        return result;
    }
    for (size_t index = 0U; index < g_state.count; ++index)
    {
        result = stop(&g_state.entries[index]);
        if (0 != result)
        {
            return finish(result);
        }
    }
    g_state = candidate;
    for (size_t index = 0U; index < count; ++index)
    {
        if (g_state.entries[index].device.b_enabled)
        {
            result = start(&g_state.entries[index]);
            if (0 != result)
            {
                return finish(result);
            }
        }
    }
    return finish(0);
}

/** @brief Update jitter from successive unsigned millisecond timestamps. */
static void record_execution (runtime_entry_t * p_entry, uint32_t now_ms)
{
    if (p_entry->b_has_execution)
    {
        const uint32_t actual = now_ms - p_entry->last_execution_ms;
        const uint32_t period = p_entry->device.period_ms;
        const uint32_t jitter =
            (actual >= period) ? (actual - period) : (period - actual);
        p_entry->status.last_jitter_ms = jitter;
        if (jitter > p_entry->status.max_jitter_ms)
        {
            p_entry->status.max_jitter_ms = jitter;
        }
    }
    p_entry->b_has_execution   = true;
    p_entry->last_execution_ms = now_ms;
}

/** @brief Execute one bound step and record any backend failure. */
static int dispatch (runtime_entry_t * p_entry)
{
    if (!p_entry->b_bound)
    {
        return -2;
    }
    ++p_entry->status.run_count;
    record_execution(p_entry, g_state.clock_ms);
    int result = p_entry->binding.p_step(&p_entry->binding.component,
                                         p_entry->binding.p_step_context);
    if (0 != result)
    {
        ++p_entry->status.failure_count;
        p_entry->status.state = UTHOME_RUNTIME_ERROR;
        return UTHOME_RUNTIME_ERR_BACKEND;
    }
    return 0;
}

/** @brief Accumulate elapsed time and execute at most one fresh bound step. */
static int tick_entry (runtime_entry_t * p_entry, uint32_t elapsed_ms)
{
    const uint32_t period = p_entry->device.period_ms;
    if ((UTHOME_RUNTIME_RUNNING != p_entry->status.state) || (0U == period))
    {
        return 0;
    }
    /* Widen before addition, since two uint32_t operands can exceed 32 bits. */
    const uint64_t total = (uint64_t)p_entry->elapsed_ms + elapsed_ms;
    /* The prior remainder is below period. The quotient therefore fits
     * uint32_t, even for UINT32_MAX elapsed ticks and a one-ms period. */
    const uint32_t due = (uint32_t)(total / period);
    /* Modulo a nonzero uint32_t period always produces a uint32_t remainder. */
    p_entry->elapsed_ms = (uint32_t)(total % period);
    if (!p_entry->b_bound)
    {
        p_entry->status.run_count += due;
        return 0;
    }
    if (0U != due)
    {
        /* Execute fresh work once per tick, avoiding catch-up bursts. */
        p_entry->status.skipped_periods += due - 1U;
        return dispatch(p_entry);
    }
    return 0;
}

/* End of file. */

