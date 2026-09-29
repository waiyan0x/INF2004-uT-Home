/**
 * @file binding_main.c
 * @brief Runtime binding regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "binding_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "uthome/runtime.h"
#include "uthome/drvreg.h"
#include "uthome/msgbus.h"
#include "hostport.h"

#define INSTANCE_COUNT 2U

typedef struct
{
    uint32_t value;
    uint32_t calls;
    bool     b_fail;
} counter_t;

typedef struct
{
    msgbus_queue_id_t h_queue;
    int               mutation_result;
} output_t;

static int read_counter (void * p_context, void * p_data, size_t capacity);
static int step (component_t * p_component, void * p_context);
static runtime_device_t device (const char * p_id, uint32_t period);
static void verify_plan_replacement (counter_t *         p_contexts,
                                     runtime_binding_t * p_bindings,
                                     runtime_device_t *  p_devices);
static void verify_step_recovery (counter_t * p_contexts);
static void verify_periodic_dispatch (counter_t *        p_contexts,
                                      output_t *         p_outputs,
                                      runtime_device_t * p_devices);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    const component_operations_t operations = {.p_read = read_counter};
    const drvreg_descriptor_t    drivers[]  = {
        {"counter", &operations, sizeof(counter_t)}};
    drvreg_registry_t registry = {0};
    assert(0 == drvreg_init(&registry, drivers, 1));
    assert(0 == runtime_init());
    assert(0 == msgbus_init());
    counter_t         contexts[]  = {{0U, 0U, 0}, {100U, 0U, 0}};
    output_t          outputs[2]  = {0};
    runtime_binding_t bindings[2] = {0};
    runtime_device_t  devices[]   = {device("room_a", 200U),
                                     device("room_b", 500U)};
    for (size_t index = 0U; index < INSTANCE_COUNT; ++index)
    {
        msgbus_queue_config_t queue = {.type      = 1U,
                                       .item_size = sizeof(uint32_t),
                                       .depth     = 4U,
                                       .overflow_policy =
                                           UTHOME_MSGBUS_DROP_OLDEST};
        assert(0 == msgbus_create_queue(&queue, &outputs[index].h_queue));
        assert(0 == drvreg_bind(&registry,
                                "counter",
                                devices[index].name,
                                &contexts[index],
                                sizeof(contexts[index]),
                                &bindings[index].component));
        bindings[index].p_step         = step;
        bindings[index].p_step_context = &outputs[index];
    }
    assert(0 == runtime_apply_bound_devices(devices, bindings, 2));
    verify_periodic_dispatch(contexts, outputs, devices);

    verify_step_recovery(contexts);

    verify_plan_replacement(contexts, bindings, devices);

    puts("Registry -> instance -> runtime step -> typed queue integration "
         "passed.");
    return 0;
}

/** @brief Read p_counter. */
static int read_counter (void * p_context, void * p_data, size_t capacity)
{
    counter_t * p_counter = p_context;
    if ((capacity < sizeof(uint32_t)) || (p_counter->b_fail))
    {
        return -1;
    }
    ++p_counter->calls;
    *(uint32_t *)p_data = ++p_counter->value;
    return 0;
}

/** @brief Step. */
static int step (component_t * p_component, void * p_context)
{
    output_t * p_output = p_context;
    uint32_t   sample   = 0U;
    /* A driver callback cannot re-enter lifecycle/configuration operations. */
    p_output->mutation_result = runtime_stop_device(p_component->name);
    if (0 != component_read(p_component, &sample, sizeof(sample)))
    {
        return -1;
    }
    return msgbus_publish(p_output->h_queue, 1U, &sample, sizeof(sample));
}

/** @brief Device. */
static runtime_device_t device (const char * p_id, uint32_t period)
{
    runtime_device_t result = {.period_ms        = period,
                               .priority         = 10U,
                               .stack_size_bytes = 3072U,
                               .b_enabled        = true};
    strcpy(result.name, p_id);
    return result;
}

/** @brief Verify plan replacement. */
static void verify_plan_replacement (counter_t *         p_contexts,
                                     runtime_binding_t * p_bindings,
                                     runtime_device_t *  p_devices)
{
    uint32_t previous = 0U;
    /* Failed whole-plan replacement must preserve the old borrowed p_contexts.
     */
    previous                               = p_contexts[0].calls;
    counter_t         rejected_contexts[2] = {0U};
    runtime_binding_t rejected[2]          = {p_bindings[0], p_bindings[1]};
    runtime_device_t  rejected_devices[2];
    for (size_t index = 0U; index < INSTANCE_COUNT; ++index)
    {
        assert(0 == runtime_get_device(p_devices[index].name,
                                       &rejected_devices[index]));
    }
    for (size_t index = 0U; index < INSTANCE_COUNT; ++index)
    {
        rejected[index].component.p_context = &rejected_contexts[index];
    }
    hostport_fail_after(UTHOME_HOST_START, 1U);
    assert(runtime_apply_bound_devices(rejected_devices, rejected, 2) != 0);
    assert((0 == rejected_contexts[0].calls) &&
           (0 == rejected_contexts[1].calls));
    assert((0 == runtime_dispatch_device("room_a")) &&
           (p_contexts[0].calls == previous + 1));
    assert(0 == rejected_contexts[0].calls);

    runtime_binding_t aliased[2]   = {p_bindings[0], p_bindings[1]};
    aliased[1].component.p_context = aliased[0].component.p_context;
    assert(runtime_apply_bound_devices(p_devices, aliased, 2) != 0);
    assert(runtime_apply_bound_devices(p_devices, NULL, 2) != 0);
    assert(runtime_bind_device("room_a", NULL) != 0); /* must quiesce first */
    assert(0 == runtime_stop_device("room_a"));
    assert(0 == runtime_bind_device("room_a", NULL));

    /* A stream has no invented polling period; readiness dispatches one step.
     */
    assert(0 == runtime_stop_device("room_b"));
    p_devices[1].b_enabled = true;
    p_devices[1].period_ms = 0U;
    assert(0 == runtime_update_device("room_b", &p_devices[1]));
    assert(0 == runtime_start_device("room_b"));
    previous = p_contexts[1].calls;
    assert((0 == runtime_tick(5000)) && (p_contexts[1].calls == previous));
    assert((0 == runtime_dispatch_device("room_b")) &&
           (p_contexts[1].calls == previous + 1));
}

/** @brief Verify step recovery. */
static void verify_step_recovery (counter_t * p_contexts)
{
    /* Failed execution retains its task resource but prevents further dispatch.
     */
    p_contexts[0].b_fail = true;
    assert(runtime_dispatch_device("room_a") != 0);
    runtime_status_t status = {0};
    assert(0 == runtime_get_status("room_a", &status));
    assert((UTHOME_RUNTIME_ERROR == status.state) &&
           (1 == status.failure_count));
    p_contexts[0].b_fail = false;
    assert(0 == runtime_restart_device("room_a"));
    assert(0 == runtime_dispatch_device("room_a"));
    assert((0 == runtime_get_status("room_a", &status)) &&
           (1 == status.restart_count));

    /* A large host-time jump performs bounded work and records skipped periods.
     */
    uint32_t previous = p_contexts[0].calls;
    assert(0 == runtime_tick(1000000));
    assert(p_contexts[0].calls == previous + 1);
    assert((0 == runtime_get_status("room_a", &status)) &&
           (status.skipped_periods > 0));
}

/** @brief Verify periodic dispatch. */
static void verify_periodic_dispatch (counter_t *        p_contexts,
                                      output_t *         p_outputs,
                                      runtime_device_t * p_devices)
{
    assert((0 == p_contexts[0].calls) &&
           (0 == p_contexts[1].calls)); /* apply performs no I/O */
    assert(0 == runtime_tick(200));
    assert((1 == p_contexts[0].calls) && (0 == p_contexts[1].calls));
    assert(UTHOME_RUNTIME_ERR_BUSY == p_outputs[0].mutation_result);
    assert(0 == runtime_tick(300));
    assert((2 == p_contexts[0].calls) && (1 == p_contexts[1].calls));
    uint32_t value = {0U};
    assert((0 == msgbus_consume_latest(
                     p_outputs[0].h_queue, 1, &value, sizeof(value))) &&
           (2 == value));
    assert((0 == msgbus_consume_latest(
                     p_outputs[1].h_queue, 1, &value, sizeof(value))) &&
           (101 == value));

    p_devices[0].period_ms = 1000U;
    assert(0 == runtime_reconfigure_device("room_a", &p_devices[0]));
    assert(0 == runtime_tick(500));
    assert((2 == p_contexts[0].calls) && (2 == p_contexts[1].calls));
    assert(0 == runtime_tick(500));
    assert((3 == p_contexts[0].calls) && (3 == p_contexts[1].calls));
    p_devices[1].b_enabled = false;
    assert(0 == runtime_reconfigure_device("room_b", &p_devices[1]));
    assert(0 == runtime_tick(1000));
    assert(3 == p_contexts[1].calls);
    assert(0 == runtime_rollback_device("room_b"));
    assert((0 == runtime_tick(500)) && (4 == p_contexts[1].calls));
}

/* End of file. */

