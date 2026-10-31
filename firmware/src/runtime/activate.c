/**
 * @file activate.c
 * @brief Replace a complete target plan and retain failed cleanup ownership.
 */
#include "activate.h"

#include <string.h>
#include "shared.h"

typedef struct
{
    size_t            index;
    rt_port_task_id_t h_task;
    bool              b_touched;
} activate_slot_t;

static activate_plan_t   g_current                           = {0};
static activate_plan_t   g_previous                          = {0};
static activate_slot_t   g_slots[UTHOME_RUNTIME_MAX_DEVICES] = {0};
static activate_status_t g_status                            = {0};
static bool              gb_initialized                      = false;
static bool              gb_busy                             = false;
static bool              gb_resources                        = false;

static bool ready (void);
static bool valid_plan (const activate_plan_t * p_plan);
static bool valid_device (const activate_device_t * p_device);
static int stop_all (void);
static int start_plan (const activate_plan_t * p_plan);
static int prepare_device (size_t index);
static int restore_previous (void);
static int run_step (void * p_context);
static int clean_step (void * p_context);

/** @brief Initialize once, with no live workers and no hardware activation. */
int activate_init (void)
{
    if ((0 != rt_port_init()) || gb_initialized)
    {
        return UTHOME_ACTIVATE_REJECTED;
    }
    if ((0 != rt_port_gate_set(false)) || (0 != shared_reset(NULL, 0U)))
    {
        return UTHOME_ACTIVATE_REJECTED;
    }
    gb_initialized = true;
    return 0;
}

/** @brief Validate, replace and, on failure, recreate the previous plan. */
int activate_apply (const activate_plan_t * p_plan)
{
    if ((!ready()) || (ACTIVATE_NEEDS_RECOVERY == g_status.state) ||
        (UINT32_MAX == g_status.generation) || (!valid_plan(p_plan)))
    {
        return UTHOME_ACTIVATE_REJECTED;
    }
    gb_busy                 = true;
    g_previous              = g_current;
    g_status.apply_error    = rt_port_gate_set(false);
    g_status.recovery_error = 0;
    if (0 == g_status.apply_error)
    {
        g_status.apply_error = stop_all();
    }
    if (0 != g_status.apply_error)
    {
        g_status.state = ACTIVATE_NEEDS_RECOVERY;
        gb_busy        = false;
        return UTHOME_ACTIVATE_RECOVERY;
    }
    g_status.apply_error = start_plan(p_plan);
    if (0 == g_status.apply_error)
    {
        gb_busy = false;
        return 0;
    }
    const int result = restore_previous();
    gb_busy          = false;
    return (0 == result) ? UTHOME_ACTIVATE_REJECTED : UTHOME_ACTIVATE_RECOVERY;
}

/** @brief Retry retained cleanup, then rebuild the last accepted plan. */
int activate_recover (void)
{
    if ((!ready()) || (ACTIVATE_NEEDS_RECOVERY != g_status.state) ||
        (UINT32_MAX == g_status.generation))
    {
        return UTHOME_ACTIVATE_REJECTED;
    }
    gb_busy          = true;
    const int result = restore_previous();
    gb_busy          = false;
    return (0 == result) ? 0 : UTHOME_ACTIVATE_RECOVERY;
}

/** @brief Release all resources without starting any previous configuration. */
int activate_clear (void)
{
    if (!ready())
    {
        return UTHOME_ACTIVATE_REJECTED;
    }
    gb_busy        = true;
    g_status.state = ACTIVATE_NEEDS_RECOVERY;
    int result     = rt_port_gate_set(false);
    if (0 == result)
    {
        result = stop_all();
    }
    if (0 == result)
    {
        result = shared_reset(NULL, 0U);
    }
    if (0 == result)
    {
        memset(&g_current, 0, sizeof(g_current));
        memset(&g_previous, 0, sizeof(g_previous));
        g_status.state          = ACTIVATE_EMPTY;
        g_status.apply_error    = 0;
        g_status.recovery_error = 0;
    }
    else
    {
        g_status.recovery_error = result;
    }
    gb_busy = false;
    return (0 == result) ? 0 : UTHOME_ACTIVATE_RECOVERY;
}

/** @brief Report retained ownership and asynchronous worker errors. */
int activate_get_status (activate_status_t * p_status)
{
    if ((!ready()) || (NULL == p_status))
    {
        return UTHOME_ACTIVATE_REJECTED;
    }
    activate_status_t status    = g_status;
    status.b_resources_retained = gb_resources;
    status.retained_instances   = 0U;
    status.failed_workers       = 0U;
    status.worker_error         = 0;
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICES; ++index)
    {
        const activate_slot_t * p_slot = &g_slots[index];
        if ((p_slot->b_touched) || (0 != p_slot->h_task))
        {
            ++status.retained_instances;
        }
        if (0 != p_slot->h_task)
        {
            int result = 0;
            if (0 != rt_port_task_result(p_slot->h_task, &result))
            {
                return UTHOME_ACTIVATE_REJECTED;
            }
            if (0 != result)
            {
                ++status.failed_workers;
                status.worker_error = result;
            }
        }
    }
    *p_status = status;
    return 0;
}

/** @brief Check task ownership before reading controller-only state. */
static bool ready (void)
{
    return (rt_port_is_owner()) && gb_initialized && (!gb_busy);
}

/** @brief Validate bounded names, callbacks and the supported worker profile.
 */
static bool valid_device (const activate_device_t * p_device)
{
    if (('\0' == p_device->component.name[0]) ||
        (NULL == memchr(p_device->component.name,
                        '\0',
                        sizeof(p_device->component.name))) ||
        (p_device->config_size > UTHOME_ACTIVATE_CONFIG_MAX))
    {
        return false;
    }
    if (!p_device->b_enabled)
    {
        return true;
    }
    const rt_port_task_config_t * p_task = &p_device->task;
    return (NULL != p_device->component.p_context) &&
           (NULL != p_device->component.p_operations) &&
           (NULL != p_device->component.p_operations->p_configure) &&
           (NULL != p_device->worker.p_step) &&
           (NULL != p_device->worker.p_cleanup) &&
           (p_device->worker.p_context == p_device->component.p_context) &&
           (p_task->priority >= 5U) && (p_task->priority <= 19U) &&
           (p_task->stack_size_bytes >= 1024U) &&
           (p_task->stack_size_bytes <= 8192U) &&
           (0U == (p_task->stack_size_bytes % 256U)) &&
           (p_task->period_ms > 0U) &&
           (p_task->period_ms <= UINT32_C(2147483647)) &&
           (p_device->heartbeat_ms >= p_task->period_ms) &&
           (p_device->heartbeat_ms <= UINT32_C(2147483647));
}

/** @brief Reject invalid plans before touching queues or working hardware. */
static bool valid_plan (const activate_plan_t * p_plan)
{
    if ((NULL == p_plan) ||
        (p_plan->device_count > UTHOME_RUNTIME_MAX_DEVICES) ||
        (p_plan->queue_count > UTHOME_MSGBUS_MAX_QUEUES))
    {
        return false;
    }

    const activate_resource_t * p_resource = &p_plan->resources;
    if (p_resource->config_size > UTHOME_ACTIVATE_CONFIG_MAX)
    {
        return false;
    }
    if (NULL != p_resource->p_release)
    {
        if ((NULL == p_resource->component.p_context) ||
            (NULL == p_resource->component.p_operations) ||
            (NULL == p_resource->component.p_operations->p_configure))
        {
            return false;
        }
    }
    else if ((NULL != p_resource->component.p_context) ||
             (NULL != p_resource->component.p_operations) ||
             (0U != p_resource->config_size))
    {
        return false;
    }
    else
    {
        /* A zero resource descriptor requires no resource callback. */
    }
    size_t         active    = 0U;
    size_t         stacks    = 0U;
    uint32_t       consumers = 0U;
    const uint32_t mask      = (1U << p_plan->queue_count) - 1U;
    for (size_t index = 0U; index < p_plan->queue_count; ++index)
    {
        const msgbus_queue_config_t * p_queue = &p_plan->queues[index];
        if ((0U == p_queue->type) || (0U == p_queue->item_size) ||
            (p_queue->item_size > UTHOME_MSGBUS_MAX_ITEM_SIZE) ||
            (0U == p_queue->depth) ||
            (p_queue->depth > UTHOME_MSGBUS_MAX_DEPTH) ||
            (UTHOME_MSGBUS_DROP_OLDEST != p_queue->overflow_policy))
        {
            return false;
        }
    }
    for (size_t index = 0U; index < p_plan->device_count; ++index)
    {
        const activate_device_t * p_device = &p_plan->devices[index];
        if (((NULL != p_resource->component.p_context) &&
             (p_resource->component.p_context ==
              p_device->component.p_context)) ||
            (!valid_device(p_device)) ||
            (0U != (p_device->consumer_mask & (~mask))))
        {
            return false;
        }
        for (size_t other = 0U; other < index; ++other)
        {
            const component_t * p_old = &p_plan->devices[other].component;
            if ((0 == strcmp(p_old->name, p_device->component.name)) ||
                ((NULL != p_device->component.p_context) &&
                 (p_old->p_context == p_device->component.p_context)))
            {
                return false;
            }
        }
        if (p_device->b_enabled)
        {
            if (0U != (consumers & p_device->consumer_mask))
            {
                return false;
            }
            consumers |= p_device->consumer_mask;
            ++active;
            stacks += p_device->task.stack_size_bytes;
        }
    }
    return (active <= UTHOME_RUNTIME_MAX_DEVICE_TASKS) &&
           (stacks <= UTHOME_RUNTIME_DEVICE_STACK_BUDGET);
}

/** @brief Attempt every release, preserving every failed handle/context. */
static int stop_all (void)
{
    int result = 0;
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICES; ++index)
    {
        activate_slot_t * p_slot   = &g_slots[index];
        int               released = 0;
        if (0 != p_slot->h_task)
        {
            released = rt_port_task_stop(p_slot->h_task);
            if (0 == released)
            {
                p_slot->h_task = 0;
            }
        }
        else if (p_slot->b_touched)
        {
            released = clean_step(p_slot);
        }
        else
        {
            /* Unconfigured or disabled entries have nothing to release. */
        }
        if ((0 == result) && (0 != released))
        {
            result = released;
        }
    }

    if ((0 == result) && gb_resources)
    {
        result = g_current.resources.p_release(
            g_current.resources.component.p_context);
        if (0 == result)
        {
            gb_resources = false;
        }
    }
    return result;
}

/** @brief Configure one context and create its still-dormant worker. */
static int prepare_device (size_t index)
{
    activate_device_t * p_device = &g_current.devices[index];
    activate_slot_t *   p_slot   = &g_slots[index];
    p_slot->index                = index;
    p_slot->b_touched            = true;
    int result                   = component_configure(
        &p_device->component, p_device->config, p_device->config_size);
    if (0 != result)
    {
        return result;
    }
    /* Plan admission bounds index to the eight configured device slots. */
    const rt_port_worker_t worker = {.p_step       = run_step,
                                     .p_cleanup    = clean_step,
                                     .p_context    = p_slot,
                                     .source_index = (uint32_t)index};
    result =
        rt_port_task_prepare_worker(&p_device->task, &worker, &p_slot->h_task);
    if (0 == result)
    {
        result = shared_watch(p_device->component.name, p_device->heartbeat_ms);
    }
    return result;
}

/** @brief Build and start a plan with every worker behind a closed barrier. */
static int start_plan (const activate_plan_t * p_plan)
{
    g_current = *p_plan;

    int result = shared_reset(g_current.queues, g_current.queue_count);
    if ((0 == result) && (NULL != g_current.resources.p_release))
    {
        gb_resources = true;
        result       = component_configure(&g_current.resources.component,
                                     g_current.resources.config,
                                     g_current.resources.config_size);
    }
    for (size_t index = 0U; (0 == result) && (index < g_current.device_count);
         ++index)
    {
        if (g_current.devices[index].b_enabled)
        {
            result = prepare_device(index);
        }
    }
    for (size_t index = 0U; (0 == result) && (index < g_current.device_count);
         ++index)
    {
        if (0 != g_slots[index].h_task)
        {
            result = rt_port_task_start(g_slots[index].h_task);
        }
    }
    for (size_t index = 0U; (0 == result) && (index < g_current.device_count);
         ++index)
    {
        if (0 != g_slots[index].h_task)
        {
            bool b_running    = false;
            int  worker_error = 0;
            if ((0 !=
                 rt_port_task_is_running(g_slots[index].h_task, &b_running)) ||
                (0 !=
                 rt_port_task_result(g_slots[index].h_task, &worker_error)) ||
                (!b_running) || (0 != worker_error))
            {
                result = -1;
            }
        }
    }
    if (0 == result)
    {
        /* Publish plan state before release can permit worker preemption. */
        g_status.state = ACTIVATE_EMPTY;
        for (size_t index = 0U; index < g_current.device_count; ++index)
        {
            if (g_current.devices[index].b_enabled)
            {
                g_status.state = ACTIVATE_ACTIVE;
            }
        }
        result = rt_port_gate_set(true);
        if (0 == result)
        {
            ++g_status.generation;
        }
    }
    return result;
}

/** @brief Reclaim partial resources before retrying the previous settings. */
static int restore_previous (void)
{
    g_status.state          = ACTIVATE_NEEDS_RECOVERY;
    g_status.recovery_error = rt_port_gate_set(false);
    if (0 == g_status.recovery_error)
    {
        g_status.recovery_error = stop_all();
    }
    if (0 == g_status.recovery_error)
    {
        g_status.recovery_error = start_plan(&g_previous);
    }
    if (0 != g_status.recovery_error)
    {
        /* Keep partial restoration owned for an explicit recover retry. */
        g_status.state = ACTIVATE_NEEDS_RECOVERY;
    }
    return g_status.recovery_error;
}

/** @brief Execute bounded driver work and heartbeat only actual progress. */
static int run_step (void * p_context)
{
    const activate_slot_t *   p_slot   = p_context;
    const activate_device_t * p_device = &g_current.devices[p_slot->index];
    const int result = p_device->worker.p_step(p_device->worker.p_context);
    if (0 == result)
    {
        const int heartbeat = shared_heartbeat(p_device->component.name);
        if ((0 != heartbeat) && (UTHOME_PORT_ERR_BUSY != heartbeat))
        {
            return heartbeat;
        }
    }
    return result;
}

/** @brief Retry cleanup without forgetting which instance still owns state. */
static int clean_step (void * p_context)
{
    activate_slot_t *         p_slot   = p_context;
    const activate_device_t * p_device = &g_current.devices[p_slot->index];
    const int result = p_device->worker.p_cleanup(p_device->worker.p_context);
    if (0 == result)
    {
        p_slot->b_touched = false;
    }
    return result;
}

/* End of file. */

