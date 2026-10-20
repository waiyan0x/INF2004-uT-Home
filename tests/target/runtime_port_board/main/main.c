/**
 * @file main.c
 * @brief Real-board acceptance of the periodic runtime task adapter.
 * @details All checks use the real kernel without peripheral I/O. The kernel
 * usermain entry point and task callback ABI are externally fixed names/types.
 * tm_printf requires UB *; its format arguments are read-only string literals.
 */
#include "main.h"
#include "c3app.h"
#include "c3store.h"
#include "c3usb.h"
#include "esp_app_desc.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <stdatomic.h>
#include <string.h>
#include "workport.h"
#include "activate.h"
#include "shared.h"
#include "uthome/sample.h"
#include "c3i2c.h"
#include "bmpinst.h"
#include "c3spi.h"
#include "c3pin.h"
#include "oledinst.h"
#include "c3uart.h"
#include "c3umem.h"
#include "uartcheck.h"
#include "cfgctrl.h"
#include "cfgjson.h"
#include "cfgplan.h"
#include "c3bind.h"
#include "c3pulse.h"
#include "c3pwm.h"
#include "hcinst.h"
#include "pwminst.h"
#include "ldinst.h"

#define WORKER_LIMIT      4U
#define ALLOCATION_CYCLES 24U

typedef struct
{
    atomic_uint       calls;
    atomic_uint       rejected_reentry;
    unsigned          cleaned;
    unsigned          cleanup_failures;
    uint32_t          delay_ms;
    int               step_error;
    rt_port_task_id_t h_task;
} context_t;

static context_t g_contexts[WORKER_LIMIT + 1U] = {0};
static unsigned  g_checks                      = 0U;

typedef struct
{
    uint32_t value;
    uint16_t source_index;
    bool     b_consumer;
    int      step_error;
} plan_config_t;

typedef struct
{
    plan_config_t config;
    atomic_uint   calls;
    atomic_uint   last_value;
    unsigned      configure_failures;
    unsigned      cleanup_failures;
    unsigned      cleaned;
} plan_context_t;

static plan_context_t  g_plan_contexts[2] = {0};
static activate_plan_t g_plan             = {0};

static void fail (const char * p_expression, int line);
static inline void wait_for_interrupt (void);
static void idle_task (INT code, void * p_context);
static int step (void * p_data);
static int cleanup (void * p_data);
static rt_port_task_config_t make_config (uint32_t period);
static void prepare (context_t * p_context, uint32_t period);
static bool is_prepared (rt_port_task_id_t h_task);
static void verify_kernel_context (void);
static void start_idle (void);
static void verify_periodic_workers (void);
static void verify_sleep_cancellation (void);
static void verify_stop_timeout (void);
static void verify_cleanup_retry (void);
static void verify_slot_reuse (void);
static void report_success (void);
static void check_result (bool b_result, const char * p_expression, int line);
static int plan_configure (void * p_data, const void * p_config, size_t size);
static int plan_step (void * p_data);
static int plan_cleanup (void * p_data);
static void make_plan (void);
static void set_value (uint32_t value, int step_error);
static void verify_barrier (void);
static void verify_activation (void);
static void verify_replacement (void);
static void verify_plan_recovery (void);
static void verify_peripheral_link (void);

static const component_operations_t g_plan_operations = {.p_configure =
                                                             plan_configure};

/* The argument is evaluated exactly once, including when checks have effects.
 */
#define CHECK(expression) (check_result((expression), #expression, __LINE__))

/** @brief Kernel entry point: execute all acceptance scenarios in order. */
EXPORT INT usermain (void)
{
    if (UTHOME_CONFIGURATION_APP)
    {
        start_idle();
        CHECK(0 == c3app_run(esp_app_get_description()->app_elf_sha256));
        return 0;
    }
    verify_kernel_context();
    verify_periodic_workers();
    verify_sleep_cancellation();
    verify_stop_timeout();
    verify_cleanup_retry();
    verify_slot_reuse();
    verify_barrier();
    verify_activation();
    verify_peripheral_link();
    report_success();
    return 0;
}

/** @brief Report an assertion failure and keep its serial diagnostic visible.
 */
static void fail (const char * p_expression, int line)
{
    tm_printf((UB *)"[port-test] FAIL line=%d %s\n", line, p_expression);
    for (;;)
    {
        (void)tk_slp_tsk(1000);
    }
}

/** @brief Isolate the architecture instruction used by the lowest-priority
 * task. */
static inline void wait_for_interrupt (void)
{
    __asm__ volatile("wfi" ::: "memory");
}

/** @brief Yield the processor while every higher-priority task is sleeping. */
static void idle_task (INT code, void * p_context)
{
    (void)code;
    (void)p_context;
    for (;;)
    {
        wait_for_interrupt();
    }
}

/** @brief Count dispatches, reject callback reentry, and inject a step delay.
 */
static int step (void * p_data)
{
    context_t * p_context = p_data;
    atomic_fetch_add(&p_context->calls, 1U);
    if (rt_port_task_stop(p_context->h_task) != 0)
    {
        atomic_fetch_add(&p_context->rejected_reentry, 1U);
    }
    if (0U != p_context->delay_ms)
    {
        (void)tk_dly_tsk((TMO)p_context->delay_ms);
    }
    return p_context->step_error;
}

/** @brief Verify serialized cleanup and inject a bounded number of failures. */
static int cleanup (void * p_data)
{
    context_t * p_context = p_data;
    CHECK(rt_port_init() != 0);
    if (0U != p_context->cleanup_failures)
    {
        --p_context->cleanup_failures;
        return -1;
    }
    ++p_context->cleaned;
    return 0;
}

/** @brief Construct a worker's scheduling configuration. */
static rt_port_task_config_t make_config (uint32_t period)
{
    return (rt_port_task_config_t){
        .priority = 10U, .stack_size_bytes = 2048U, .period_ms = period};
}

/** @brief Prepare a worker and retain its generation-aware task handle. */
static void prepare (context_t * p_context, uint32_t period)
{
    rt_port_worker_t worker = {
        .p_step = step, .p_cleanup = cleanup, .p_context = p_context};
    rt_port_task_config_t settings = make_config(period);
    CHECK(0 ==
          rt_port_task_prepare_worker(&settings, &worker, &p_context->h_task));
}

/** @brief Query whether the adapter still owns the task resource. */
static bool is_prepared (rt_port_task_id_t h_task)
{
    bool b_value = false;
    CHECK(0 == rt_port_task_is_prepared(h_task, &b_value));
    return b_value;
}

/** @brief Share the existing idle entry point between build modes. */
static void start_idle (void)
{
    T_CTSK idle_config = {.tskatr  = TA_HLNG | TA_RNG0,
                          .task    = (FP)idle_task,
                          .itskpri = 32,
                          .stksz   = 1024};
    ID     h_idle      = tk_cre_tsk(&idle_config);
    CHECK((h_idle > 0) && (E_OK == tk_sta_tsk(h_idle, 0)));
}

/** @brief Verify kernel context. */
static void verify_kernel_context (void)
{
    tm_printf((UB *)"\n[port-test] BEGIN\n");
    start_idle();
    CHECK(E_OK ==
          tk_chg_pri(TSK_SELF, 20)); /* Workers can preempt their creator. */
    CHECK(0 == rt_port_init());
    CHECK(rt_port_transaction_begin() != 0);
    CHECK(rt_port_transaction_commit() != 0);
    CHECK(rt_port_transaction_rollback() != 0);
    uint64_t before = 0U;
    uint64_t after  = 0U;
    CHECK(0 == rt_port_now_ms(&before));
    CHECK(E_OK == tk_dly_tsk(100));
    CHECK((0 == rt_port_now_ms(&after)) && (after - before >= 100) &&
          (after - before < 150));
    tm_printf((UB *)"[port-test] monotonic delay=%u ms\n",
              (unsigned)(after - before));
}

/** @brief Verify periodic workers. */
static void verify_periodic_workers (void)
{
    prepare(&g_contexts[0], 40);
    prepare(&g_contexts[1], 90);
    CHECK(rt_port_init() != 0);
    CHECK(E_OK == tk_dly_tsk(30));
    CHECK((0 == atomic_load(&g_contexts[0].calls)) &&
          (0 == atomic_load(&g_contexts[1].calls)));
    bool b_running = true;
    CHECK((0 == rt_port_task_is_running(g_contexts[0].h_task, &b_running)) &&
          (!b_running));
    rt_port_task_config_t changed = make_config(80);
    CHECK(0 == rt_port_task_reconfigure(g_contexts[1].h_task, &changed));
    CHECK(0 == rt_port_task_start(g_contexts[0].h_task));
    CHECK(0 == rt_port_task_start(g_contexts[1].h_task));
    CHECK(rt_port_task_start(g_contexts[0].h_task) != 0);
    CHECK(rt_port_task_reconfigure(g_contexts[1].h_task, &changed) != 0);
    CHECK(E_OK == tk_dly_tsk(220));
    CHECK(0 == rt_port_task_stop(g_contexts[0].h_task));
    CHECK(0 == rt_port_task_stop(g_contexts[1].h_task));
    unsigned fast = atomic_load(&g_contexts[0].calls);
    unsigned slow = atomic_load(&g_contexts[1].calls);
    CHECK((fast >= 4) && (slow >= 2) && (fast > slow));
    CHECK(atomic_load(&g_contexts[0].rejected_reentry) == fast);
    CHECK((1 == g_contexts[0].cleaned) && (1 == g_contexts[1].cleaned));
    CHECK((E_OK == tk_dly_tsk(100)) &&
          (atomic_load(&g_contexts[0].calls) == fast));
    tm_printf((UB *)"[port-test] independent workers fast=%u slow=%u; cleanup "
                    "verified\n",
              fast,
              slow);
}

/** @brief Verify sleep cancellation. */
static void verify_sleep_cancellation (void)
{
    uint64_t          before  = 0U;
    uint64_t          after   = 0U;
    rt_port_task_id_t h_stale = g_contexts[0].h_task;
    prepare(&g_contexts[0], 1000);
    CHECK((g_contexts[0].h_task != h_stale) &&
          (rt_port_task_stop(h_stale) != 0));
    CHECK(0 == rt_port_task_start(g_contexts[0].h_task));
    CHECK(0 == rt_port_now_ms(&before));
    CHECK(0 == rt_port_task_stop(g_contexts[0].h_task));
    CHECK((0 == rt_port_now_ms(&after)) &&
          (after - before < UTHOME_PORT_STOP_TIMEOUT_MS));
    tm_printf((UB *)"[port-test] sleeping worker cancellation=%u ms\n",
              (unsigned)(after - before));
}

/** @brief Verify stop timeout. */
static void verify_stop_timeout (void)
{
    g_contexts[2].delay_ms = 800;
    prepare(&g_contexts[2], 100);
    CHECK(0 == rt_port_task_start(g_contexts[2].h_task));
    CHECK(1 == atomic_load(&g_contexts[2].calls));
    CHECK(UTHOME_PORT_ERR_TIMEOUT == rt_port_task_stop(g_contexts[2].h_task));
    CHECK((is_prepared(g_contexts[2].h_task)) && (0 == g_contexts[2].cleaned));
    CHECK(E_OK == tk_dly_tsk(350));
    CHECK((0 == rt_port_task_stop(g_contexts[2].h_task)) &&
          (1 == g_contexts[2].cleaned));
    tm_printf((UB *)"[port-test] timeout retained ownership; retry cleaned and "
                    "deleted\n");
}

/** @brief Verify cleanup retry. */
static void verify_cleanup_retry (void)
{
    bool b_running                 = true;
    g_contexts[3].step_error       = -123;
    g_contexts[3].cleanup_failures = 1;
    prepare(&g_contexts[3], 50);
    CHECK(0 == rt_port_task_start(g_contexts[3].h_task));
    CHECK((0 == rt_port_task_is_running(g_contexts[3].h_task, &b_running)) &&
          (!b_running));
    int result = 0;
    CHECK((0 == rt_port_task_result(g_contexts[3].h_task, &result)) &&
          (-123 == result));
    CHECK(UTHOME_PORT_ERR_CLEANUP == rt_port_task_stop(g_contexts[3].h_task));
    CHECK((is_prepared(g_contexts[3].h_task)) && (0 == g_contexts[3].cleaned));
    CHECK(rt_port_task_start(g_contexts[3].h_task) != 0);
    CHECK((0 == rt_port_task_stop(g_contexts[3].h_task)) &&
          (1 == g_contexts[3].cleaned));
    tm_printf((UB *)"[port-test] step failure and cleanup retry verified\n");
}

/** @brief Verify slot reuse. */
static void verify_slot_reuse (void)
{
    for (unsigned cycle = 0U; cycle < ALLOCATION_CYCLES; ++cycle)
    {
        for (unsigned index = 0U; index < WORKER_LIMIT; ++index)
        {
            prepare(&g_contexts[index], 100);
        }
        rt_port_worker_t extra = {
            .p_step = step, .p_cleanup = cleanup, .p_context = &g_contexts[4]};
        rt_port_task_config_t settings   = make_config(100);
        rt_port_task_id_t     h_rejected = -99;
        CHECK((-2 ==
               rt_port_task_prepare_worker(&settings, &extra, &h_rejected)) &&
              (-99 == h_rejected));
        for (unsigned index = 0U; index < WORKER_LIMIT; ++index)
        {
            CHECK(0 == rt_port_task_stop(g_contexts[index].h_task));
        }
    }
}

/** @brief Report success. */
static void report_success (void)
{
    CHECK(0 == rt_port_init());
    tm_printf((UB *)"[port-test] PASS checks=%u; 24 four-slot "
                    "allocation/release cycles\n",
              g_checks);
    unsigned tick = 0U;
    for (;;)
    {
        tm_printf((UB *)"[port-test] alive %u\n", tick);
        (void)tk_dly_tsk(1000);
        ++tick;
    }
}

/** @brief Evaluate one already-computed condition and preserve the check count.
 */
static void check_result (bool b_result, const char * p_expression, int line)
{
    ++g_checks;
    if (!b_result)
    {
        fail(p_expression, line);
    }
}

/** @brief Configure a dormant fixture context using copied scalar settings. */
static int plan_configure (void * p_data, const void * p_config, size_t size)
{
    plan_context_t * p_context = p_data;
    if (sizeof(p_context->config) != size)
    {
        return -1;
    }
    memcpy(&p_context->config, p_config, size);
    atomic_store(&p_context->calls, 0U);
    atomic_store(&p_context->last_value, 0U);
    CHECK(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    if (0U != p_context->configure_failures)
    {
        --p_context->configure_failures;
        return -23;
    }
    return 0;
}

/** @brief Publish or consume one real shared queue item from a worker. */
static int plan_step (void * p_data)
{
    plan_context_t * p_context = p_data;
    const unsigned   sequence  = atomic_fetch_add(&p_context->calls, 1U);
    if (0 != p_context->config.step_error)
    {
        return p_context->config.step_error;
    }
    sample_distance_t sample = {0};
    int               result = 0;
    if (p_context->config.b_consumer)
    {
        result =
            shared_consume(0, UTHOME_SAMPLE_DISTANCE, &sample, sizeof(sample));
        if (0 == result)
        {
            atomic_store(&p_context->last_value, sample.distance_mm);
        }
        else if (UTHOME_MSGBUS_ERR_EMPTY == result)
        {
            result = 0;
        }
        else
        {
            /* Propagate genuine queue failures below; contention is retried. */
        }
    }
    else
    {
        result = rt_port_now_ms(&sample.header.timestamp_ms);
        if (0 == result)
        {
            sample.header.sequence     = sequence;
            sample.header.source_index = p_context->config.source_index;
            sample.header.status       = UTHOME_SAMPLE_OK;
            sample.distance_mm         = p_context->config.value;
            result                     = shared_publish(
                0, UTHOME_SAMPLE_DISTANCE, &sample, sizeof(sample));
        }
    }
    return (UTHOME_PORT_ERR_BUSY == result) ? 0 : result;
}

/** @brief Model retryable resource release in the lifecycle owner. */
static int plan_cleanup (void * p_data)
{
    plan_context_t * p_context = p_data;
    CHECK(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    if (0U != p_context->cleanup_failures)
    {
        --p_context->cleanup_failures;
        return -24;
    }
    ++p_context->cleaned;
    return 0;
}

/** @brief Put copied values into the candidate, never into a live context. */
static void set_value (uint32_t value, int step_error)
{
    const plan_config_t producer = {.value = value, .source_index = 0U};
    const plan_config_t consumer = {
        .source_index = 1U, .b_consumer = true, .step_error = step_error};
    memcpy(g_plan.devices[0].config, &producer, sizeof(producer));
    memcpy(g_plan.devices[1].config, &consumer, sizeof(consumer));
}

/** @brief Resolve a producer and consumer fixture with independent contexts. */
static void make_plan (void)
{
    g_plan.device_count = 2U;
    g_plan.queue_count  = 1U;
    g_plan.queues[0]    = (msgbus_queue_config_t){UTHOME_SAMPLE_DISTANCE,
                                                  sizeof(sample_distance_t),
                                                  4U,
                                                  UTHOME_MSGBUS_DROP_OLDEST};
    for (size_t index = 0U; index < 2U; ++index)
    {
        activate_device_t * p_device     = &g_plan.devices[index];
        p_device->component.p_context    = &g_plan_contexts[index];
        p_device->component.p_operations = &g_plan_operations;
        p_device->worker                 = (rt_port_worker_t){
            plan_step, plan_cleanup, &g_plan_contexts[index]};
        p_device->task         = (rt_port_task_config_t){10U, 2048U, 20U};
        p_device->heartbeat_ms = 500U;
        p_device->config_size  = sizeof(plan_config_t);
        p_device->b_enabled    = true;
    }
    /* Both fixed literals fit the zero-initialized instance-name arrays. */
    memcpy(g_plan.devices[0].component.name, "source", sizeof("source"));
    memcpy(g_plan.devices[1].component.name, "sink", sizeof("sink"));
    g_plan.devices[1].consumer_mask  = 1U;
    g_plan.devices[1].task.priority  = 11U;
    g_plan.devices[1].task.period_ms = 10U;
    set_value(111U, 0);
}

/** @brief Prove started workers perform no steps until collective release. */
static void verify_barrier (void)
{
    atomic_store(&g_contexts[0].calls, 0U);
    atomic_store(&g_contexts[1].calls, 0U);
    CHECK(0 == rt_port_gate_set(false));
    prepare(&g_contexts[0], 10U);
    prepare(&g_contexts[1], 10U);
    CHECK(0 == rt_port_task_start(g_contexts[0].h_task));
    CHECK(E_OK == tk_dly_tsk(30));
    CHECK(0U == atomic_load(&g_contexts[0].calls));
    CHECK(0 == rt_port_task_start(g_contexts[1].h_task));
    CHECK(E_OK == tk_dly_tsk(30));
    CHECK((0U == atomic_load(&g_contexts[0].calls)) &&
          (0U == atomic_load(&g_contexts[1].calls)));
    CHECK(0 == rt_port_gate_set(true));
    CHECK(E_OK == tk_dly_tsk(40));
    CHECK((atomic_load(&g_contexts[0].calls) > 0U) &&
          (atomic_load(&g_contexts[1].calls) > 0U));
    CHECK(0 == rt_port_task_stop(g_contexts[0].h_task));
    CHECK(0 == rt_port_task_stop(g_contexts[1].h_task));
    tm_printf((UB *)"[activation-test] start barrier passed\n");
}

/** @brief Verify copied settings, disable/enable and asynchronous faults. */
static void verify_replacement (void)
{
    activate_status_t before = {0};
    activate_status_t after  = {0};
    CHECK(0 == activate_get_status(&before));
    g_plan.devices[0].task.period_ms = 0U;
    CHECK(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    CHECK(0 == activate_get_status(&after));
    CHECK(before.generation == after.generation);
    g_plan.devices[0].task.period_ms = 20U;
    g_plan.devices[1].b_enabled      = false;
    set_value(222U, 0);
    CHECK(0 == activate_apply(&g_plan));
    const unsigned sink_calls = atomic_load(&g_plan_contexts[1].calls);
    CHECK(E_OK == tk_dly_tsk(60));
    CHECK(sink_calls == atomic_load(&g_plan_contexts[1].calls));
    sample_distance_t sample = {0};
    CHECK(0 ==
          shared_consume(0, UTHOME_SAMPLE_DISTANCE, &sample, sizeof(sample)));
    CHECK(222U == sample.distance_mm);
    CHECK(0 == activate_get_status(&after));
    CHECK(1U == after.retained_instances);
    g_plan.devices[1].b_enabled = true;
    set_value(333U, 0);
    CHECK(0 == activate_apply(&g_plan));
    CHECK(E_OK == tk_dly_tsk(80));
    CHECK(333U == atomic_load(&g_plan_contexts[1].last_value));
    set_value(333U, -12);
    CHECK(0 == activate_apply(&g_plan));
    CHECK(E_OK == tk_dly_tsk(40));
    CHECK(0 == activate_get_status(&after));
    CHECK((1U == after.failed_workers) && (-12 == after.worker_error));
    set_value(333U, 0);
    CHECK(0 == activate_apply(&g_plan));
}

/** @brief Restore old values and retain failed cleanup until explicit retry. */
static void verify_plan_recovery (void)
{
    activate_status_t status = {0};
    set_value(444U, 0);
    g_plan_contexts[1].configure_failures = 1U;
    CHECK(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    CHECK(E_OK == tk_dly_tsk(60));
    CHECK(333U == atomic_load(&g_plan_contexts[1].last_value));
    set_value(333U, 0);
    g_plan_contexts[0].cleanup_failures = 1U;
    CHECK(UTHOME_ACTIVATE_RECOVERY == activate_apply(&g_plan));
    CHECK(0 == activate_get_status(&status));
    CHECK((ACTIVATE_NEEDS_RECOVERY == status.state) &&
          (1U == status.retained_instances));
    const unsigned calls = atomic_load(&g_plan_contexts[0].calls);
    CHECK(E_OK == tk_dly_tsk(30));
    CHECK(calls == atomic_load(&g_plan_contexts[0].calls));
    CHECK(shared_reset(NULL, 0U) != 0);
    CHECK(UTHOME_ACTIVATE_REJECTED == activate_apply(&g_plan));
    CHECK(0 == activate_recover());
    g_plan_contexts[0].configure_failures = 2U;
    CHECK(UTHOME_ACTIVATE_RECOVERY == activate_apply(&g_plan));
    CHECK(0 == activate_get_status(&status));
    CHECK(ACTIVATE_NEEDS_RECOVERY == status.state);
    CHECK(0 == activate_recover());
    CHECK(E_OK == tk_dly_tsk(60));
    CHECK(333U == atomic_load(&g_plan_contexts[1].last_value));
}

/** @brief Run target plan acceptance with actual preemption and typed queues.
 */
static void verify_activation (void)
{
    make_plan();
    CHECK(0 == activate_init());
    CHECK(0 == activate_apply(&g_plan));
    CHECK(E_OK == tk_dly_tsk(100));
    CHECK(atomic_load(&g_plan_contexts[0].calls) >= 3U);
    CHECK(111U == atomic_load(&g_plan_contexts[1].last_value));
    verify_replacement();
    verify_plan_recovery();
    for (unsigned cycle = 0U; cycle < 6U; ++cycle)
    {
        set_value(500U + cycle, 0);
        CHECK(0 == activate_apply(&g_plan));
        CHECK(E_OK == tk_dly_tsk(50));
        CHECK((500U + cycle) == atomic_load(&g_plan_contexts[1].last_value));
    }
    const activate_plan_t empty = {0};
    CHECK(0 == activate_apply(&empty));
    activate_status_t status = {0};
    CHECK(0 == activate_get_status(&status));
    CHECK((ACTIVATE_EMPTY == status.state) &&
          (0U == status.retained_instances));
    CHECK(E_OK == tk_dly_tsk(600));
    CHECK(0 == shared_check());
    health_snapshot_t health = {0};
    CHECK(0 == shared_snapshot(&health));
    CHECK(0U == health.total_events);
    tm_printf((UB *)"[activation-test] PASS generation=%u; typed queue, "
                    "replacement, recovery, six cycles and empty stop\n",
              status.generation);
}

/** @brief Retain real peripheral code in the link without touching GPIOs. */
static void verify_peripheral_link (void)
{
    uint8_t byte  = 0U;
    size_t  count = 0U;
    CHECK(-1 == c3store_operations()->p_read(NULL, 0U, 0U, &byte, 1U));
    CHECK(-1 == c3store_operations()->p_write(NULL, 0U, 0U, &byte, 1U));
    CHECK(-1 == c3store_operations()->p_erase(NULL, 0U));
    CHECK(-1 == c3store_operations()->p_sync(NULL));
    CHECK(-1 == c3usb_operations()->p_read(NULL, &byte, 1U, &count));
    CHECK(-1 == c3usb_operations()->p_write(NULL, &byte, 1U, &count));
    /* Startup creation runs once; the repeated call must create nothing. */
    CHECK(0 == c3i2c_init());
    CHECK(0 == c3i2c_init());
    i2cbus_t * p_bus = NULL;
    CHECK(I2CBUS_INVALID == c3i2c_open(NULL, &p_bus));
    CHECK(NULL == p_bus);
    CHECK(I2CBUS_STATE == c3i2c_close(NULL));
    const drvreg_descriptor_t * p_driver = bmpinst_descriptor();
    CHECK(sizeof(bmpinst_t) == p_driver->context_size);
    CHECK(I2CBUS_INVALID == bmpinst_cleanup(NULL));
    /* SPI follows the same rule: one startup creation, none on repeat. */
    CHECK(0 == c3spi_init());
    CHECK(0 == c3spi_init());
    spibus_t * p_spi = NULL;
    CHECK(SPIBUS_INVALID == c3spi_open(NULL, &p_spi));
    CHECK(NULL == p_spi);
    CHECK(SPIBUS_STATE == c3spi_close(NULL));
    CHECK(sizeof(oledinst_t) == oledinst_descriptor()->context_size);
    CHECK(SPIBUS_INVALID == oledinst_cleanup(NULL));
    CHECK(NULL != c3uart_port());
    CHECK(sizeof(ldinst_t) == ldinst_descriptor()->context_size);
    CHECK(UARTIO_INVALID == ldinst_cleanup(NULL));
    CHECK(-1 == c3pin_open(NULL, 0U, false));
    CHECK(-1 == c3pin_set(NULL, false));
    CHECK(-1 == c3pin_close(NULL));
    CHECK(0 == c3uart_init());
    CHECK(0 == c3uart_init());
    CHECK(0 == c3umem_seal());
    CHECK(NULL == c3umem_serial_alloc(UTHOME_C3UMEM_BYTES));
    CHECK(NULL == c3umem_record_alloc(UTHOME_C3UMEM_BYTES));
    c3umem_serial_free(NULL);
    c3umem_record_free(NULL);
    CHECK(0 == c3umem_seal());
#if !UTHOME_CONFIGURATION_APP
    CHECK(0 == uartcheck_run());
#endif
    CHECK(0 == c3pulse_startup());
    CHECK(0 == c3pulse_startup());
    CHECK(0 == c3pwm_startup());
    CHECK(0 == c3pwm_startup());
    CHECK(NULL != c3pulse_port(0U));
    CHECK(NULL != c3pulse_port(1U));
    CHECK(NULL == c3pulse_port(2U));
    CHECK(NULL != c3pwm_port(0U));
    CHECK(NULL != c3pwm_port(3U));
    CHECK(NULL == c3pwm_port(4U));
    CHECK(sizeof(hcinst_t) == hcinst_descriptor()->context_size);
    CHECK(sizeof(pwminst_t) == pwminst_descriptor()->context_size);
    CHECK(PULSEIO_INVALID == hcinst_cleanup(NULL));
    CHECK(PWMIO_INVALID == pwminst_cleanup(NULL));
    CHECK(-1 == c3pin_input_open(NULL, 0U));
    CHECK(-1 == c3pin_get(NULL, NULL));
    CHECK(-1 == c3pin_input_close(NULL));
    CHECK(-2 == cfgwire_feed(NULL, 0U, 0U));
    CHECK(-2 == cfgwire_expire(NULL, 0U));
    CHECK(-2 == cfgwire_request(NULL, NULL));
    CHECK(-2 == cfgwire_reply(NULL, NULL, NULL));
    CHECK(CFGSTORE_INVALID == cfgstore_load(NULL, NULL, 0U, NULL));
    CHECK(CFGSTORE_INVALID == cfgstore_save(NULL, NULL, 0U, 0U));
    CHECK(-1 == cfgctrl_init(NULL, NULL, NULL, NULL));
    CHECK(CFGCTRL_REJECTED == cfgctrl_restore(NULL));

    CHECK(-1 == cfgctrl_handle(NULL, NULL, NULL));
    CHECK(0 == cfgjson_init());
    CHECK(NULL == cfgjson_open(NULL, 0U));
    CHECK(-1 == cfgplan_read(NULL, 0U, 0U, NULL));
    CHECK(31U == cfgbind_mask());
    CHECK(NULL != c3bind_operations());
    CHECK(-1 == cfgbind_init(NULL, c3bind_operations()));
    CHECK(NULL != cfgbind_operations());
    CHECK(-1 == cfgbind_operations()->p_validate(NULL, NULL, 0U));
    CHECK(1 == cfgbind_operations()->p_activate(NULL));
    CHECK(-1 == cfgbind_operations()->p_recover(NULL));
}

/* End of file. */

