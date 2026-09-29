/**
 * @file hostport.c
 * @brief Model checked task transactions and deterministic host failures.
 *
 * This serialized backend creates no operating-system or kernel threads.
 */
#include "hostport.h"
#include "uthome/runtime.h"
#include <string.h>

typedef struct
{
    bool                  b_in_use;
    bool                  b_running;
    rt_port_task_config_t config;
} host_port_task_t;

typedef struct
{
    bool   b_armed;
    size_t remaining;
} host_fault_t;

static host_port_task_t g_tasks[UTHOME_RUNTIME_MAX_DEVICE_TASKS]      = {0};
static host_port_task_t g_checkpoint[UTHOME_RUNTIME_MAX_DEVICE_TASKS] = {0};
static bool             gb_transaction                                = false;
static host_fault_t     g_faults[UTHOME_HOST_OPERATION_COUNT]         = {0};

static bool fail_now (hostport_operation_t operation);
static bool valid_handle (rt_port_task_id_t h_task_id);
static host_port_task_t * task_for (rt_port_task_id_t h_task_id);
static bool valid_config (const rt_port_task_config_t * p_config);

/** @brief Reset serialized task metadata and deterministic fault state. */
int rt_port_init (void)
{
    memset(g_tasks, 0, sizeof(g_tasks));
    memset(g_checkpoint, 0, sizeof(g_checkpoint));
    gb_transaction = false;
    hostport_clear_faults();
    return 0;
}

/** @brief Reserve one host slot and return its handle without starting. */
int rt_port_task_prepare (const rt_port_task_config_t * p_config,
                          rt_port_task_id_t *           ph_task_id)
{
    if ((!valid_config(p_config)) || (NULL == ph_task_id))
    {
        return -1;
    }
    if (fail_now(UTHOME_HOST_PREPARE))
    {
        return -1;
    }
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICE_TASKS; ++index)
    {
        if (!g_tasks[index].b_in_use)
        {
            g_tasks[index] =
                (host_port_task_t){.b_in_use = true, .config = *p_config};
            /* A validated four-slot index fits the signed 32-bit handle. */
            *ph_task_id = (rt_port_task_id_t)index;
            return 0;
        }
    }
    return -2;
}

/** @brief Mark an existing task running unless start failure is armed. */
int rt_port_task_start (rt_port_task_id_t h_task_id)
{
    host_port_task_t * const p_task = task_for(h_task_id);
    if ((NULL == p_task) || (fail_now(UTHOME_HOST_START)))
    {
        return -1;
    }
    p_task->b_running = true;
    return 0;
}

/** @brief Release a prepared task unless stop failure is armed. */
int rt_port_task_stop (rt_port_task_id_t h_task_id)
{
    host_port_task_t * const p_task = task_for(h_task_id);
    if ((NULL == p_task) || (fail_now(UTHOME_HOST_STOP)))
    {
        return -1;
    }
    memset(p_task, 0, sizeof(*p_task));
    return 0;
}

/** @brief Update validated metadata for an existing task. */
int rt_port_task_reconfigure (rt_port_task_id_t             h_task_id,
                              const rt_port_task_config_t * p_config)
{
    host_port_task_t * const p_task = task_for(h_task_id);
    if ((NULL == p_task) || (!valid_config(p_config)) ||
        (fail_now(UTHOME_HOST_RECONFIGURE)))
    {
        return -1;
    }
    p_task->config = *p_config;
    return 0;
}

/** @brief Report reservation state for an in-range host handle. */
int rt_port_task_is_prepared (rt_port_task_id_t h_task_id, bool * pb_prepared)
{
    if ((NULL == pb_prepared) || (!valid_handle(h_task_id)))
    {
        return -1;
    }
    *pb_prepared = g_tasks[h_task_id].b_in_use;
    return 0;
}

/** @brief Report whether a prepared host task is marked running. */
int rt_port_task_is_running (rt_port_task_id_t h_task_id, bool * pb_running)
{
    host_port_task_t * const p_task = task_for(h_task_id);
    if ((NULL == p_task) || (NULL == pb_running))
    {
        return -1;
    }
    *pb_running = p_task->b_running;
    return 0;
}

/** @brief Save all task metadata before serialized lifecycle changes. */
int rt_port_transaction_begin (void)
{
    if (gb_transaction)
    {
        return -1;
    }
    memcpy(g_checkpoint, g_tasks, sizeof(g_tasks));
    gb_transaction = true;
    return 0;
}

/** @brief Accept task changes and release the transaction guard. */
int rt_port_transaction_commit (void)
{
    if (!gb_transaction)
    {
        return -1;
    }
    gb_transaction = false;
    return 0;
}

/** @brief Restore all checkpointed task metadata if rollback succeeds. */
int rt_port_transaction_rollback (void)
{
    if ((!gb_transaction) || (fail_now(UTHOME_HOST_ROLLBACK)))
    {
        return -1;
    }
    memcpy(g_tasks, g_checkpoint, sizeof(g_tasks));
    gb_transaction = false;
    return 0;
}

/** @brief Arm one operation failure after the requested call count. */
void hostport_fail_after (hostport_operation_t operation, size_t calls)
{
    if ((operation >= 0U) && (operation < UTHOME_HOST_OPERATION_COUNT))
    {
        g_faults[operation].b_armed   = true;
        g_faults[operation].remaining = calls;
    }
}

/** @brief Disarm every deterministic failure and reset its countdown. */
void hostport_clear_faults (void)
{
    memset(g_faults, 0, sizeof(g_faults));
}

/** @brief Count reserved slots regardless of their running state. */
size_t hostport_task_count (void)
{
    size_t count = 0U;
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICE_TASKS; ++index)
    {
        if (g_tasks[index].b_in_use)
        {
            ++count;
        }
    }
    return count;
}

/** @brief Consume a configured failure only after its successful-call delay. */
static bool fail_now (hostport_operation_t operation)
{
    if (!g_faults[operation].b_armed)
    {
        return false;
    }
    if (0U != g_faults[operation].remaining)
    {
        --g_faults[operation].remaining;
        return false;
    }
    g_faults[operation].b_armed = false;
    return true;
}

/** @brief Validate a signed opaque handle before using it as an array index. */
static bool valid_handle (rt_port_task_id_t h_task_id)
{
    /* The left operand excludes negatives, so conversion loses no value. */
    return (h_task_id >= 0) &&
           ((uint32_t)h_task_id < UTHOME_RUNTIME_MAX_DEVICE_TASKS);
}

/** @brief Return a prepared host task while rejecting invalid handles. */
static host_port_task_t * task_for (rt_port_task_id_t h_task_id)
{
    if ((!valid_handle(h_task_id)) || (!g_tasks[h_task_id].b_in_use))
    {
        return NULL;
    }
    return &g_tasks[h_task_id];
}

/** @brief Require the supported host priority and aligned stack bounds. */
static bool valid_config (const rt_port_task_config_t * p_config)
{
    return (NULL != p_config) && (p_config->priority >= 1U) &&
           (p_config->priority <= 31U) &&
           (p_config->stack_size_bytes >= 1024U) &&
           (p_config->stack_size_bytes <= 8192U) &&
           (0U == (p_config->stack_size_bytes % 256U));
}

/* End of file. */

