#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/component.h"
#include "uthome/health.h"
#include "uthome/msgbus.h"
#include "uthome/runtime.h"


typedef struct {
    int sensor_value;
    uthome_msgbus_queue_id_t queue_id;

} fake_sensor_context_t;


static int fake_sensor_read(
    void *context,
    void *data,
    size_t data_capacity)
{
    if (context == NULL ||
        data == NULL ||
        data_capacity < sizeof(int)) {

        return -1;
    }

    fake_sensor_context_t *ctx =
        context;

    *(int *)data =
        ctx->sensor_value;

    return 0;
}


static int fake_sensor_publish(
    void *context,
    const void *data,
    size_t data_size)
{
    if (context == NULL ||
        data == NULL ||
        data_size != sizeof(int)) {

        return -1;
    }

    fake_sensor_context_t *ctx =
        context;

    return uthome_msgbus_publish(
        ctx->queue_id,
        1,
        data,
        data_size
    ) == UTHOME_MSGBUS_OK
        ? 0
        : -1;
}


static uthome_runtime_device_t
make_runtime_device(void)
{
    uthome_runtime_device_t device = {
        .period_ms = 2000,
        .priority = 3,
        .stack_size_bytes = 2048,
        .enabled = true
    };

    strncpy(
        device.id,
        "temperature_sensor",
        sizeof(device.id) - 1
    );

    device.id[
        sizeof(device.id) - 1
    ] = '\0';

    return device;
}


int main(void)
{
    /*
     * Initialise subsystems.
     */
    assert(
        uthome_runtime_init() == 0
    );

    assert(
        uthome_msgbus_init() ==
        UTHOME_MSGBUS_OK
    );

    assert(
        uthome_health_init() ==
        UTHOME_HEALTH_OK
    );

    assert(
        uthome_health_watchdog_init() ==
        UTHOME_HEALTH_OK
    );


    /*
     * Create a very small telemetry queue.
     *
     * Depth 2 makes overflow easy to test.
     */
    uthome_msgbus_queue_config_t
        queue_config = {

            .type = 1,
            .item_size = sizeof(int),
            .depth = 2,
            .overflow_policy =
                UTHOME_MSGBUS_DROP_OLDEST
        };


    uthome_msgbus_queue_id_t queue_id;


    assert(
        uthome_msgbus_create_queue(
            &queue_config,
            &queue_id
        ) == UTHOME_MSGBUS_OK
    );


    /*
     * Create fake sensor component.
     */
    fake_sensor_context_t sensor_context = {
        .sensor_value = 25,
        .queue_id = queue_id
    };


    const uthome_component_operations_t
        sensor_operations = {

            .configure = NULL,
            .read = fake_sensor_read,
            .write = NULL,
            .publish = fake_sensor_publish
        };


    uthome_component_t sensor_component = {
        .context = &sensor_context,
        .operations = &sensor_operations
    };


    strncpy(
        sensor_component.id,
        "temperature_sensor",
        sizeof(sensor_component.id) - 1
    );

    sensor_component.id[
        sizeof(sensor_component.id) - 1
    ] = '\0';


    /*
     * Register + start runtime task.
     */
    uthome_runtime_device_t device =
        make_runtime_device();


    assert(
        uthome_runtime_register_device(
            &device
        ) == 0
    );


    assert(
        uthome_runtime_start_device(
            "temperature_sensor"
        ) == 0
    );


    /*
     * Register task heartbeat monitoring.
     */
    assert(
        uthome_health_watch_task(
            "temperature_sensor",
            3000,
            0
        ) == UTHOME_HEALTH_OK
    );


    /*
     * SAMPLE 1
     */
    int value = 0;

    assert(
        uthome_component_read(
            &sensor_component,
            &value,
            sizeof(value)
        ) == UTHOME_COMPONENT_OK
    );

    assert(value == 25);


    assert(
        uthome_component_publish(
            &sensor_component,
            &value,
            sizeof(value)
        ) == UTHOME_COMPONENT_OK
    );


    /*
     * Heartbeat after successful work.
     */
    assert(
        uthome_health_task_heartbeat(
            "temperature_sensor",
            1000
        ) == UTHOME_HEALTH_OK
    );


    /*
     * SAMPLE 2
     */
    sensor_context.sensor_value = 26;

    assert(
        uthome_component_read(
            &sensor_component,
            &value,
            sizeof(value)
        ) == UTHOME_COMPONENT_OK
    );

    assert(
        uthome_component_publish(
            &sensor_component,
            &value,
            sizeof(value)
        ) == UTHOME_COMPONENT_OK
    );


    /*
     * Queue is now full:
     *
     * [25] [26]
     *
     * SAMPLE 3 causes DROP_OLDEST:
     *
     * [26] [27]
     */
    sensor_context.sensor_value = 27;

    assert(
        uthome_component_read(
            &sensor_component,
            &value,
            sizeof(value)
        ) == UTHOME_COMPONENT_OK
    );

    assert(
        uthome_component_publish(
            &sensor_component,
            &value,
            sizeof(value)
        ) == UTHOME_COMPONENT_OK
    );


    /*
     * Confirm one queue drop.
     */
    uthome_msgbus_stats_t stats;

    assert(
        uthome_msgbus_get_stats(
            queue_id,
            &stats
        ) == UTHOME_MSGBUS_OK
    );

    assert(stats.dropped_count == 1);
    assert(stats.current_depth == 2);


    /*
     * Health detects the queue overflow.
     */
    uint32_t previous_drop_count = 0;

    assert(
        uthome_health_check_queue(
            queue_id,
            2000,
            &previous_drop_count
        ) == UTHOME_HEALTH_OK
    );


    uint32_t overflow_count = 0;

    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_QUEUE_OVERFLOW,
            &overflow_count
        ) == UTHOME_HEALTH_OK
    );

    assert(overflow_count == 1);


    /*
     * Verify newest telemetry survived.
     */
    int latest_value = 0;

    assert(
        uthome_msgbus_consume_latest(
            queue_id,
            1,
            &latest_value,
            sizeof(latest_value)
        ) == UTHOME_MSGBUS_OK
    );

    assert(latest_value == 27);


    /*
     * Runtime task should still be healthy
     * and running.
     */
    uthome_runtime_status_t status;

    assert(
        uthome_runtime_get_status(
            "temperature_sensor",
            &status
        ) == 0
    );

    assert(
        status.state ==
        UTHOME_RUNTIME_RUNNING
    );


    printf(
        "Full runtime integration test passed!\n"
    );

    return 0;
}