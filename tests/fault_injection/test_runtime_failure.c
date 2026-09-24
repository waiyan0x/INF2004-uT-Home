#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/health.h"
#include "uthome/runtime.h"


static uthome_runtime_device_t make_device(void)
{
    uthome_runtime_device_t device = {
        .period_ms = 2000,
        .priority = 3,
        .stack_size_bytes = 2048,
        .enabled = true
    };

    strncpy(
        device.id,
        "sensor_task",
        sizeof(device.id) - 1
    );

    device.id[
        sizeof(device.id) - 1
    ] = '\0';

    return device;
}


int main(void)
{
    uthome_runtime_status_t status;
    uthome_runtime_resource_snapshot_t resources;

    uint32_t heartbeat_missed_count = 0;


    /*
     * Initialise runtime + health.
     */
    assert(
        uthome_runtime_init() == 0
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
     * Register and start sensor task.
     */
    uthome_runtime_device_t device =
        make_device();


    assert(
        uthome_runtime_register_device(
            &device
        ) == 0
    );


    assert(
        uthome_runtime_start_device(
            "sensor_task"
        ) == 0
    );


    /*
     * Watch heartbeat.
     *
     * Task must heartbeat at least once
     * every 3000 ms.
     */
    assert(
        uthome_health_watch_task(
            "sensor_task",
            3000,
            0
        ) == UTHOME_HEALTH_OK
    );


    /*
     * Task works normally first.
     */
    assert(
        uthome_health_task_heartbeat(
            "sensor_task",
            1000
        ) == UTHOME_HEALTH_OK
    );


    /*
     * 2500 ms after heartbeat:
     * still healthy.
     */
    assert(
        uthome_health_check_heartbeats(
            3500
        ) == UTHOME_HEALTH_OK
    );


    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
            &heartbeat_missed_count
        ) == UTHOME_HEALTH_OK
    );

    assert(heartbeat_missed_count == 0);


    /*
     * FAULT INJECTION:
     *
     * Pretend the sensor task stops
     * responding.
     *
     * At 4001 ms after the last heartbeat,
     * watchdog must detect a failure.
     */
    assert(
        uthome_health_check_heartbeats(
            4001
        ) == UTHOME_HEALTH_OK
    );


    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
            &heartbeat_missed_count
        ) == UTHOME_HEALTH_OK
    );

    assert(heartbeat_missed_count == 1);


    /*
     * Runtime records the corresponding
     * task failure.
     */
    assert(
        uthome_runtime_record_failure(
            "sensor_task"
        ) == 0
    );


    assert(
        uthome_runtime_get_status(
            "sensor_task",
            &status
        ) == 0
    );


    assert(
        status.state ==
        UTHOME_RUNTIME_ERROR
    );

    assert(status.failure_count == 1);


    /*
     * Recovery:
     *
     * Restart the failed device.
     */
    assert(
        uthome_runtime_restart_device(
            "sensor_task"
        ) == 0
    );


    assert(
        uthome_runtime_get_status(
            "sensor_task",
            &status
        ) == 0
    );


    assert(
        status.state ==
        UTHOME_RUNTIME_RUNNING
    );

    assert(status.failure_count == 1);
    assert(status.restart_count == 1);


    /*
     * Recovery must not leak task slots.
     */
    assert(
        uthome_runtime_get_resources(
            &resources
        ) == 0
    );

    assert(resources.active_tasks == 1);

    assert(
        resources.configured_stack_bytes ==
        2048
    );


    /*
     * Task resumes heartbeats after recovery.
     */
    assert(
        uthome_health_task_heartbeat(
            "sensor_task",
            5000
        ) == UTHOME_HEALTH_OK
    );


    assert(
        uthome_health_check_heartbeats(
            7000
        ) == UTHOME_HEALTH_OK
    );


    /*
     * No second missed-heartbeat event.
     */
    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
            &heartbeat_missed_count
        ) == UTHOME_HEALTH_OK
    );

    assert(heartbeat_missed_count == 1);


    printf(
        "Runtime fault-injection recovery test passed!\n"
    );

    return 0;
}