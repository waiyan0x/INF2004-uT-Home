#include <assert.h>
#include <stdio.h>
#include <string.h>

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
        "sensor",
        sizeof(device.id) - 1
    );

    device.id[
        sizeof(device.id) - 1
    ] = '\0';

    return device;
}


int main(void)
{
    uthome_runtime_device_t device =
        make_device();

    uthome_runtime_status_t status;

    uthome_runtime_resource_snapshot_t resources;


    assert(
        uthome_runtime_init() == 0
    );


    assert(
        uthome_runtime_register_device(
            &device
        ) == 0
    );


    assert(
        uthome_runtime_start_device(
            "sensor"
        ) == 0
    );


    /*
     * Initial state.
     */
    assert(
        uthome_runtime_get_status(
            "sensor",
            &status
        ) == 0
    );

    assert(
        status.state ==
        UTHOME_RUNTIME_RUNNING
    );

    assert(status.failure_count == 0);
    assert(status.restart_count == 0);


    /*
     * Simulate first runtime failure.
     */
    assert(
        uthome_runtime_record_failure(
            "sensor"
        ) == 0
    );


    assert(
        uthome_runtime_get_status(
            "sensor",
            &status
        ) == 0
    );

    assert(
        status.state ==
        UTHOME_RUNTIME_ERROR
    );

    assert(status.failure_count == 1);
    assert(status.restart_count == 0);


    /*
     * Restart after failure.
     */
    assert(
        uthome_runtime_restart_device(
            "sensor"
        ) == 0
    );


    assert(
        uthome_runtime_get_status(
            "sensor",
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
     * Restart must still leave exactly
     * one task resource.
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
     * Simulate another failure.
     */
    assert(
        uthome_runtime_record_failure(
            "sensor"
        ) == 0
    );


    assert(
        uthome_runtime_restart_device(
            "sensor"
        ) == 0
    );


    assert(
        uthome_runtime_get_status(
            "sensor",
            &status
        ) == 0
    );

    assert(status.failure_count == 2);
    assert(status.restart_count == 2);

    assert(
        status.state ==
        UTHOME_RUNTIME_RUNNING
    );


    printf(
        "All runtime restart tests passed!\n"
    );

    return 0;
}