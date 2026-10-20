/**
 * @file tkfake_main.c
 * @brief Inject kernel API failures without simulating worker execution.
 * @details The tk_* entry points deliberately retain the external kernel API.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "tkfake_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <tk/tkernel.h>
#include "workport.h"
#include "activate.h"
#include "shared.h"
#include "periph.h"
#include "oledtest.h"
#include "owntest.h"
#include "uarttest.h"
#include "gpiotest.h"
#include "bindtest.h"
#include "servtest.h"
#include "uthome/sample.h"

#define FAKE_TASK_LIMIT        8U
#define FAKE_WORKER_LIMIT      4U
#define FAKE_LARGE_STACK_LIMIT 2U
#define FAKE_RUNNING_STATE     4U

static ID       gh_caller                 = 1;
static uint32_t g_system_state            = 0U;
static uint64_t g_clock_ms                = 0U;
static uint32_t g_states[FAKE_TASK_LIMIT] = {0U};
static uint32_t g_creates                 = 0U;
static uint32_t g_starts                  = 0U;
static uint32_t g_fail_create_at          = 0U;
static uint32_t g_fail_start_at           = 0U;
static uint32_t g_deletes                 = 0U;
static uint32_t g_cleanups                = 0U;
static uint32_t g_steps                   = 0U;
static bool     gb_fail_create            = false;
static bool     gb_fail_start             = false;
static bool     gb_fail_delete            = false;
static bool     gb_fail_priority          = false;
static bool     gb_fail_cleanup           = false;
static bool     gb_stuck                  = false;

typedef struct
{
    uint32_t value;
    unsigned configured;
    unsigned cleaned;
    unsigned configure_failures;
    unsigned cleanup_failures;
} plan_context_t;

static plan_context_t  g_plan_contexts[2] = {0};
static activate_plan_t g_plan             = {0};

static int plan_configure (void * p_data, const void * p_config, size_t size);
static int plan_step (void * p_data);
static int plan_cleanup (void * p_data);
static void make_plan (void);
static void check_rejected_plans (void);
static void check_plan_failures (void);
static void check_recovery (void);
static int check_shared_reentry (void * p_data);

static const component_operations_t g_plan_operations = {.p_configure =
                                                             plan_configure};

static int step (void * p_context);
static int cleanup (void * p_context);
static void verify_activation (void);

/** @brief Return the injected execution context to the adapter. */
ER tk_ref_sys (T_RSYS * p_state)
{
    p_state->sysstat  = g_system_state;
    p_state->runtskid = gh_caller;
    return E_OK;
}

/** @brief Expose the test clock using the kernel's split time representation.
 */
ER tk_get_otm (SYSTIM * p_time)
{
    p_time->hi = (int32_t)(g_clock_ms >> 32U);
    p_time->lo = (uint32_t)g_clock_ms;
    return E_OK;
}

/** @brief Advance the injected clock without scheduling a worker. */
ER tk_slp_tsk (TMO timeout)
{
    g_clock_ms += (uint32_t)timeout;
    return E_TMOUT;
}

/** @brief Reject accidental worker execution in this failure-injection test. */
void tk_ext_tsk (void)
{
    assert(!"Worker execution belongs to board tests");
}

/** @brief Allocate a fake dormant task unless allocation failure is injected.
 */
ID tk_cre_tsk (const T_CTSK * p_config)
{
    assert((p_config->task) && (p_config->exinf));
    ++g_creates;
    if (gb_fail_create || (g_creates == g_fail_create_at))
    {
        return -33;
    }
    for (ID h_task = 1; h_task < (ID)FAKE_TASK_LIMIT; ++h_task)
    {
        if (0U == g_states[h_task])
        {
            g_states[h_task] = TTS_DMT;
            return h_task;
        }
    }
    return -33;
}

/** @brief Start a fake task or return the injected start failure. */
ER tk_sta_tsk (ID h_task, INT code)
{
    (void)code;
    ++g_starts;
    if (gb_fail_start || (g_starts == g_fail_start_at))
    {
        return -41;
    }
    g_states[h_task] = FAKE_RUNNING_STATE;
    return E_OK;
}

/** @brief Return a fake task's current lifecycle state. */
ER tk_ref_tsk (ID h_task, T_RTSK * p_state)
{
    p_state->tskstat = g_states[h_task];
    return g_states[h_task] ? E_OK : -42;
}

/** @brief Allow stop to complete unless a stuck task was requested. */
ER tk_wup_tsk (ID h_task)
{
    if (!gb_stuck)
    {
        g_states[h_task] = TTS_DMT;
    }
    return E_OK;
}

/** @brief Advance virtual time during a stop timeout. */
ER tk_dly_tsk (TMO delay)
{
    g_clock_ms += (uint32_t)delay;
    return E_OK;
}

/** @brief Release a fake task or preserve it when deletion fails. */
ER tk_del_tsk (ID h_task)
{
    ++g_deletes;
    if (gb_fail_delete)
    {
        return -41;
    }
    g_states[h_task] = 0U;
    return E_OK;
}

/** @brief Return the injected priority-change result. */
ER tk_chg_pri (ID h_task, int priority)
{
    (void)h_task;
    (void)priority;
    return gb_fail_priority ? -41 : E_OK;
}

/** @brief Exercise allocation, stop, cleanup, and deletion failures. */
int main (void)
{
    int contexts[FAKE_WORKER_LIMIT + 1U] = {0};

    rt_port_task_config_t config = {
        .priority = 10U, .stack_size_bytes = 4096U, .period_ms = 100U};
    rt_port_worker_t worker = {
        .p_step = step, .p_cleanup = cleanup, .p_context = &contexts[0]};
    rt_port_task_id_t h_task                     = -99;
    rt_port_task_id_t h_tasks[FAKE_WORKER_LIMIT] = {0};
    bool              b_prepared                 = false;
    assert(rt_port_task_prepare_worker(&config, &worker, &h_task) != 0);
    g_system_state = 4U;
    assert(rt_port_init() != 0);
    g_system_state = 0U;
    assert(0 == rt_port_init());
    assert(rt_port_transaction_begin() != 0);
    assert(rt_port_task_prepare(&config, &h_task) != 0);
    gb_fail_create = true;
    assert((rt_port_task_prepare_worker(&config, &worker, &h_task) != 0) &&
           (-99 == h_task));
    gb_fail_create   = false;
    config.period_ms = 0U;
    assert(rt_port_task_prepare_worker(&config, &worker, &h_task) != 0);
    config.period_ms = 100U;
    assert(0 == rt_port_task_prepare_worker(&config, &worker, &h_task));
    assert((0 == g_steps) && (0 == g_cleanups));
    assert(rt_port_init() != 0);
    assert(rt_port_task_prepare_worker(&config, &worker, &h_tasks[0]) !=
           0); /* Alias */
    gh_caller = 2;
    assert(rt_port_task_stop(h_task) != 0);
    gh_caller      = 1;
    g_system_state = 1U;
    assert(rt_port_task_start(h_task) != 0);
    g_system_state   = 0U;
    gb_fail_priority = true;
    assert(rt_port_task_reconfigure(h_task, &config) != 0);
    gb_fail_priority = false;
    gb_fail_start    = true;
    assert(rt_port_task_start(h_task) != 0);
    gb_fail_start = false;
    assert(0 == rt_port_task_start(h_task));
    assert(rt_port_task_start(h_task) != 0);
    assert(rt_port_task_reconfigure(h_task, &config) != 0);
    gb_stuck = true;
    assert(UTHOME_PORT_ERR_TIMEOUT == rt_port_task_stop(h_task));
    assert((UTHOME_PORT_STOP_TIMEOUT_MS == g_clock_ms) && (0 == g_deletes) &&
           (0 == g_cleanups));
    assert((0 == rt_port_task_is_prepared(h_task, &b_prepared)) && b_prepared);
    gb_stuck        = false;
    gb_fail_cleanup = true;
    assert((UTHOME_PORT_ERR_CLEANUP == rt_port_task_stop(h_task)) &&
           (0 == g_deletes));
    assert(rt_port_task_start(h_task) != 0);
    gb_fail_cleanup = false;
    gb_fail_delete  = true;
    assert((rt_port_task_stop(h_task) != 0) && (2 == g_cleanups));
    gb_fail_delete = false;
    assert((0 == rt_port_task_stop(h_task)) &&
           (2 == g_cleanups)); /* No double cleanup */
    assert((0 == rt_port_task_is_prepared(h_task, &b_prepared)) &&
           (!b_prepared));
    assert((0 == rt_port_task_prepare_worker(&config, &worker, &h_tasks[0])) &&
           (h_tasks[0] != h_task));
    assert(rt_port_task_stop(h_task) != 0); /* Stale handle */
    for (size_t index = 1U; index < FAKE_WORKER_LIMIT; ++index)
    {
        worker.p_context = &contexts[index];
        assert(0 ==
               rt_port_task_prepare_worker(&config, &worker, &h_tasks[index]));
    }
    worker.p_context = &contexts[4];
    assert(-2 == rt_port_task_prepare_worker(&config, &worker, &h_task));
    for (size_t index = 0U; index < FAKE_WORKER_LIMIT; ++index)
    {
        assert(0 == rt_port_task_stop(h_tasks[index]));
    }
    config.stack_size_bytes = 8192U;
    for (size_t index = 0U; index < FAKE_LARGE_STACK_LIMIT; ++index)
    {
        worker.p_context = &contexts[index];
        assert(0 ==
               rt_port_task_prepare_worker(&config, &worker, &h_tasks[index]));
    }
    config.stack_size_bytes = 1024U;
    worker.p_context        = &contexts[2];
    assert(-2 == rt_port_task_prepare_worker(&config, &worker, &h_task));
    for (size_t index = 0U; index < FAKE_LARGE_STACK_LIMIT; ++index)
    {
        assert(0 == rt_port_task_stop(h_tasks[index]));
    }
    uint64_t now = 0U;
    g_clock_ms   = UINT64_C(0x100000007);
    assert((0 == rt_port_now_ms(&now)) && (now == g_clock_ms));
    assert(0 == rt_port_init());
    assert((0 == g_steps) && (g_creates > 0));
    puts(
        "Target-port failure injection passed; no kernel execution simulated.");
    verify_activation();
    periph_run();
    oledtest_run();
    owntest_run();
    uarttest_run();
    gpiotest_run();
    bindtest_run(&g_clock_ms);
    servtest_run(&g_clock_ms);
    return 0;
}

/** @brief Count unexpected worker dispatches in the API-only fixture. */
static int step (void * p_context)
{
    (void)p_context;
    ++g_steps;
    return 0;
}

/** @brief Count cleanup retries and verify that cleanup cannot reenter init. */
static int cleanup (void * p_context)
{
    (void)p_context;
    ++g_cleanups;
    assert(rt_port_init() != 0);
    return gb_fail_cleanup ? -1 : 0;
}

/** @brief Configure copied scalar settings and reject lifecycle reentry. */
static int plan_configure (void * p_data, const void * p_config, size_t size)
{
    plan_context_t * p_context = p_data;
    assert(sizeof(p_context->value) == size);
    memcpy(&p_context->value, p_config, size);
    ++p_context->configured;
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    if (0U != p_context->configure_failures)
    {
        --p_context->configure_failures;
        return -23;
    }
    return 0;
}

/** @brief Leave worker execution to the real-board acceptance fixture. */
static int plan_step (void * p_data)
{
    /* The API-only kernel double never executes this callback. */
    (void)p_data;
    assert(!"Plan steps belong to board tests");
    return 0;
}

/** @brief Model retryable release without losing a failed context. */
static int plan_cleanup (void * p_data)
{
    plan_context_t * p_context = p_data;
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    if (0U != p_context->cleanup_failures)
    {
        --p_context->cleanup_failures;
        return -24;
    }
    ++p_context->cleaned;
    return 0;
}

/** @brief Build two independent resolved instances and one sample queue. */
static void make_plan (void)
{
    g_plan.device_count = 2U;
    g_plan.queue_count  = 1U;
    g_plan.queues[0]    = (msgbus_queue_config_t){UTHOME_SAMPLE_ENVIRONMENT,
                                                  sizeof(sample_environment_t),
                                                  2U,
                                                  UTHOME_MSGBUS_DROP_OLDEST};
    for (size_t index = 0U; index < 2U; ++index)
    {
        activate_device_t * p_device     = &g_plan.devices[index];
        p_device->component.p_context    = &g_plan_contexts[index];
        p_device->component.p_operations = &g_plan_operations;
        p_device->worker                 = (rt_port_worker_t){
            plan_step, plan_cleanup, &g_plan_contexts[index]};
        p_device->task         = (rt_port_task_config_t){10U, 2048U, 50U};
        p_device->heartbeat_ms = 1000U;
        p_device->b_enabled    = true;
        p_device->config_size  = sizeof(uint32_t);
        const uint32_t value   = 7U;
        memcpy(p_device->config, &value, sizeof(value));
    }
    /* Both fixed literals fit the zero-initialized name arrays. */
    memcpy(g_plan.devices[0].component.name, "first", sizeof("first"));
    memcpy(g_plan.devices[1].component.name, "second", sizeof("second"));
}

/** @brief Keep valid running state untouched on invalid admission. */
static void check_rejected_plans (void)
{
    const uint32_t creates          = g_creates;
    g_plan.devices[1].consumer_mask = 1U;
    g_plan.devices[0].consumer_mask = 1U;
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    g_plan.devices[0].consumer_mask  = 0U;
    g_plan.devices[1].consumer_mask  = 0U;
    g_plan.devices[1].task.period_ms = 0U;
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    g_plan.devices[1].task.period_ms      = 50U;
    g_plan.devices[1].component.p_context = &g_plan_contexts[0];
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    g_plan.devices[1].component.p_context = &g_plan_contexts[1];
    g_plan.device_count                   = UTHOME_RUNTIME_MAX_DEVICES + 1U;
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    g_plan.device_count = 2U;
    assert(creates == g_creates);
    gh_caller = 2;
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    gh_caller = 1;
}

/** @brief Restore previous settings after partial configure/create/start. */
static void check_plan_failures (void)
{
    const uint32_t value = 99U;
    memcpy(g_plan.devices[0].config, &value, sizeof(value));
    g_plan_contexts[1].configure_failures = 1U;
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    assert(7U == g_plan_contexts[0].value);
    g_fail_create_at = g_creates + 2U;
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    assert(7U == g_plan_contexts[0].value);
    g_fail_start_at = g_starts + 2U;
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    assert(7U == g_plan_contexts[0].value);
    activate_status_t status = {0};
    assert(0 == activate_get_status(&status));
    assert((ACTIVATE_ACTIVE == status.state) &&
           (2U == status.retained_instances) && (0U == status.failed_workers));
}

/** @brief Recover cleanup, deletion and restoration failures explicitly. */
static void check_recovery (void)
{
    activate_status_t status            = {0};
    g_plan_contexts[0].cleanup_failures = 1U;
    assert(UTHOME_ACTIVATE_RECOVERY == activate_apply(&g_plan));
    assert(0 == activate_get_status(&status));
    assert((ACTIVATE_NEEDS_RECOVERY == status.state) &&
           (1U == status.retained_instances));
    assert(shared_reset(NULL, 0U) != 0);
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    assert(0 == activate_recover());
    assert(7U == g_plan_contexts[0].value);
    gb_fail_delete = true;
    assert(UTHOME_ACTIVATE_RECOVERY == activate_apply(&g_plan));
    const unsigned cleaned = g_plan_contexts[0].cleaned;
    gb_fail_delete         = false;
    assert(0 == activate_recover());
    assert(cleaned == g_plan_contexts[0].cleaned);
    g_plan_contexts[0].configure_failures = 2U;
    assert(UTHOME_ACTIVATE_RECOVERY == activate_apply(&g_plan));
    assert(0 == activate_get_status(&status));
    assert(ACTIVATE_NEEDS_RECOVERY == status.state);
    assert(0 == activate_recover());
    assert(7U == g_plan_contexts[0].value);
    gb_stuck = true;
    assert(UTHOME_ACTIVATE_RECOVERY == activate_apply(&g_plan));
    gb_stuck = false;
    assert(0 == activate_recover());
}

/** @brief Prove contention returns promptly and blocks callback lifecycle. */
static int check_shared_reentry (void * p_data)
{
    /* This callback needs no user state; it probes the wrapper guard. */
    (void)p_data;
    assert(UTHOME_PORT_ERR_BUSY == shared_check());
    assert(!rt_port_is_owner());
    assert(rt_port_init() != 0);
    assert(rt_port_gate_set(true) != 0);
    return 0;
}

/** @brief Exercise complete replacement and fault recovery with real modules.
 */
static void verify_activation (void)
{
    make_plan();
    assert(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    assert(0 == activate_init());
    assert(UTHOME_ACTIVATE_REJECTED == activate_init());
    assert(0 == activate_apply(&g_plan));
    check_rejected_plans();
    check_plan_failures();
    check_recovery();
    assert(0 == rt_port_shared_call(check_shared_reentry, NULL));
    g_system_state = 4U;
    assert(shared_check() != 0);
    g_system_state              = 0U;
    const activate_plan_t empty = {0};
    assert(0 == activate_apply(&empty));
    activate_status_t status = {0};
    assert(0 == activate_get_status(&status));
    assert((ACTIVATE_EMPTY == status.state) &&
           (0U == status.retained_instances));
    g_clock_ms += 2000U;
    assert(0 == shared_check());
    health_snapshot_t health = {0};
    assert(0 == shared_snapshot(&health));
    assert(0U == health.total_events);
    puts("Target plan admission, restoration and retained-ownership tests "
         "passed.");
}

/* End of file. */

