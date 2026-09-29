/**
 * @file rtinteg_main.c
 * @brief Runtime integration regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "rtinteg_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/component.h"
#include "uthome/watchdog.h"
#include "uthome/hlthbus.h"
#include "uthome/msgbus.h"
#include "uthome/runtime.h"

typedef struct
{
    int               sensor_value;
    msgbus_queue_id_t h_queue;

} fake_sensor_context_t;

static int
fake_sensor_read (void * p_context, void * p_data, size_t data_capacity);
static int
fake_sensor_publish (void * p_context, const void * p_data, size_t data_size);
static runtime_device_t make_runtime_device (void);
static void verify_telemetry (component_t *           p_component,
                              fake_sensor_context_t * p_context);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    /* Initialise subsystems. */
    assert(0 == runtime_init());

    assert(UTHOME_MSGBUS_OK == msgbus_init());

    assert(UTHOME_HEALTH_OK == health_init());

    assert(UTHOME_HEALTH_OK == watchdog_init());

    /* Create a very small telemetry queue. Depth 2 makes overflow easy to test.
     */
    msgbus_queue_config_t queue_config = {

        .type            = 1U,
        .item_size       = sizeof(int),
        .depth           = 2U,
        .overflow_policy = UTHOME_MSGBUS_DROP_OLDEST};

    msgbus_queue_id_t h_queue = {0};

    assert(UTHOME_MSGBUS_OK == msgbus_create_queue(&queue_config, &h_queue));

    /* Create fake sensor component. */
    fake_sensor_context_t sensor_context = {.sensor_value = 25,
                                            .h_queue      = h_queue};

    const component_operations_t sensor_operations = {

        .p_configure = NULL,
        .p_read      = fake_sensor_read,
        .p_write     = NULL,
        .p_publish   = fake_sensor_publish};

    component_t sensor_component = {.p_context    = &sensor_context,
                                    .p_operations = &sensor_operations};

    strncpy(sensor_component.name,
            "temperature_sensor",
            sizeof(sensor_component.name) - 1U);

    sensor_component.name[sizeof(sensor_component.name) - 1U] = '\0';

    /* Register + start runtime task. */
    runtime_device_t device = make_runtime_device();

    assert(0 == runtime_register_device(&device));

    assert(0 == runtime_start_device("temperature_sensor"));

    /* Register task heartbeat monitoring. */
    assert(UTHOME_HEALTH_OK ==
           watchdog_watch_task("temperature_sensor", 3000, 0));

    verify_telemetry(&sensor_component, &sensor_context);

    /* Runtime task should still be healthy and running. */
    runtime_status_t status = {0};

    assert(0 == runtime_get_status("temperature_sensor", &status));

    assert(UTHOME_RUNTIME_RUNNING == status.state);

    printf("Full runtime integration test passed!\n");

    return 0;
}

/** @brief Fake sensor read. */
static int
fake_sensor_read (void * p_context, void * p_data, size_t data_capacity)
{
    if ((NULL == p_context) || (NULL == p_data) ||
        (data_capacity < sizeof(int)))
    {
        return -1;
    }

    fake_sensor_context_t * p_instance = p_context;

    *(int *)p_data = p_instance->sensor_value;

    return 0;
}

/** @brief Fake sensor publish. */
static int
fake_sensor_publish (void * p_context, const void * p_data, size_t data_size)
{
    if ((NULL == p_context) || (NULL == p_data) || (data_size != sizeof(int)))
    {
        return -1;
    }

    fake_sensor_context_t * p_instance = p_context;

    return UTHOME_MSGBUS_OK ==
                   msgbus_publish(p_instance->h_queue, 1U, p_data, data_size)
               ? 0
               : -1;
}

/** @brief Make runtime device. */
static runtime_device_t make_runtime_device (void)
{
    runtime_device_t device = {.period_ms        = 2000U,
                               .priority         = 3U,
                               .stack_size_bytes = 2048U,
                               .b_enabled        = true};

    strncpy(device.name, "temperature_sensor", sizeof(device.name) - 1U);

    device.name[sizeof(device.name) - 1U] = '\0';

    return device;
}

/** @brief Verify telemetry. */
static void verify_telemetry (component_t *           p_component,
                              fake_sensor_context_t * p_context)
{
    const msgbus_queue_id_t h_queue = p_context->h_queue;
    /* SAMPLE 1 */
    int value = 0;

    assert(UTHOME_COMPONENT_OK ==
           component_read(&(*p_component), &value, sizeof(value)));

    assert(25 == value);

    assert(UTHOME_COMPONENT_OK ==
           component_publish(&(*p_component), &value, sizeof(value)));

    /* Heartbeat after successful work. */
    assert(UTHOME_HEALTH_OK == watchdog_heartbeat("temperature_sensor", 1000));

    /* SAMPLE 2 */
    (*p_context).sensor_value = 26;

    assert(UTHOME_COMPONENT_OK ==
           component_read(&(*p_component), &value, sizeof(value)));

    assert(UTHOME_COMPONENT_OK ==
           component_publish(&(*p_component), &value, sizeof(value)));

    /* Queue is now full: [25] [26] SAMPLE 3 causes DROP_OLDEST: [26] [27] */
    (*p_context).sensor_value = 27;

    assert(UTHOME_COMPONENT_OK ==
           component_read(&(*p_component), &value, sizeof(value)));

    assert(UTHOME_COMPONENT_OK ==
           component_publish(&(*p_component), &value, sizeof(value)));

    /* Confirm one queue drop. */
    msgbus_stats_t stats = {0U};

    assert(UTHOME_MSGBUS_OK == msgbus_get_stats(h_queue, &stats));

    assert(1 == stats.dropped_count);
    assert(2 == stats.current_depth);

    /* Health detects the queue overflow. */
    uint32_t previous_drop_count = 0U;

    assert(UTHOME_HEALTH_OK ==
           hlthbus_check_queue(h_queue, 2000, &previous_drop_count));

    uint32_t overflow_count = 0U;

    assert(
        UTHOME_HEALTH_OK ==
        health_get_event_count(UTHOME_HEALTH_QUEUE_OVERFLOW, &overflow_count));

    assert(1 == overflow_count);

    /* Verify newest telemetry survived. */
    int latest_value = 0;

    assert(
        UTHOME_MSGBUS_OK ==
        msgbus_consume_latest(h_queue, 1, &latest_value, sizeof(latest_value)));

    assert(27 == latest_value);
}

/* End of file. */

