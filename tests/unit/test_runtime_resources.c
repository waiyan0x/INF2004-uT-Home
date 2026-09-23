#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/runtime.h"


static uthome_runtime_device_t make_device(
    const char *id,
    size_t stack_size)
{
    uthome_runtime_device_t device = {
        .period_ms = 2000,
        .priority = 3,
        .stack_size_bytes = stack_size,
        .enabled = true
    };

    strncpy(
        device.id,
        id,
        sizeof(device.id) - 1
    );

    device.id[
        sizeof(device.id) - 1
    ] = '\0';

    return device;
}


int main(void)
{
    uthome_runtime_resource_snapshot_t resources;

    uthome_runtime_device_t sensor =
        make_device(
            "sensor",
            2048
        );

    uthome_runtime_device_t actuator =
        make_device(
            "actuator",
            3072
        );


    /*
     * Initialise runtime.
     */
    assert(
        uthome_runtime_init() == 0
    );


    /*
     * Initially:
     *
     * no devices
     * no active tasks
     * no stack allocated
     */
    assert(
        uthome_runtime_get_resources(
            &resources
        ) == 0
    );

    assert(resources.registered_devices == 0);
    assert(resources.active_tasks == 0);

    assert(
        resources.free_task_slots ==
        UTHOME_RUNTIME_MAX_DEVICE_TASKS
    );

    assert(
        resources.configured_stack_bytes == 0
    );


    /*
     * Register two devices.
     */
    assert(
        uthome_runtime_register_device(
            &sensor
        ) == 0
    );

    assert(
        uthome_runtime_register_device(
            &actuator
        ) == 0
    );


    /*
     * Registration alone should not
     * allocate task resources.
     */
    assert(
        uthome_runtime_get_resources(
            &resources
        ) == 0
    );

    assert(resources.registered_devices == 2);
    assert(resources.active_tasks == 0);


    /*
     * Start sensor.
     *
     * Sensor requires 2048 bytes.
     */
    assert(
        uthome_runtime_start_device(
            "sensor"
        ) == 0
    );

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
     * Start actuator.
     *
     * Total stack:
     *
     * 2048 + 3072 = 5120
     */
    assert(
        uthome_runtime_start_device(
            "actuator"
        ) == 0
    );

    assert(
        uthome_runtime_get_resources(
            &resources
        ) == 0
    );

    assert(resources.active_tasks == 2);

    assert(
        resources.configured_stack_bytes ==
        5120
    );


    /*
     * Stop sensor.
     *
     * Only actuator remains:
     * 3072 bytes.
     */
    assert(
        uthome_runtime_stop_device(
            "sensor"
        ) == 0
    );

    assert(
        uthome_runtime_get_resources(
            &resources
        ) == 0
    );

    assert(resources.active_tasks == 1);

    assert(
        resources.configured_stack_bytes ==
        3072
    );


    printf(
        "All runtime resource tests passed!\n"
    );

    return 0;
}