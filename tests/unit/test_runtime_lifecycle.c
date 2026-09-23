#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/runtime.h"


static uthome_runtime_device_t make_device(
    const char *id,
    uint32_t period_ms,
    size_t stack_size)
{
    uthome_runtime_device_t device = {
        .period_ms = period_ms,
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
    uthome_runtime_status_t status;


    uthome_runtime_device_t sensor =
        make_device(
            "sensor",
            2000,
            2048
        );


    assert(
        uthome_runtime_init() == 0
    );


    /*
     * Register device.
     *
     * Registration should not allocate
     * a task resource yet.
     */
    assert(
        uthome_runtime_register_device(
            &sensor
        ) == 0
    );


    assert(
        uthome_runtime_get_resources(
            &resources
        ) == 0
    );

    assert(resources.active_tasks == 0);


    /*
     * PREPARE
     *
     * Reserve one task slot but
     * do not start execution.
     */
    assert(
        uthome_runtime_prepare_device(
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


    assert(
        uthome_runtime_get_status(
            "sensor",
            &status
        ) == 0
    );

    assert(
        status.state ==
        UTHOME_RUNTIME_READY
    );


    /*
     * START
     */
    assert(
        uthome_runtime_start_device(
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


    /*
     * RECONFIGURE
     *
     * Old:
     * period = 2000 ms
     * stack  = 2048 bytes
     *
     * New:
     * period = 5000 ms
     * stack  = 4096 bytes
     */
    uthome_runtime_device_t updated =
        sensor;

    updated.period_ms = 5000;
    updated.stack_size_bytes = 4096;


    assert(
        uthome_runtime_reconfigure_device(
            "sensor",
            &updated
        ) == 0
    );


    assert(
        uthome_runtime_get_resources(
            &resources
        ) == 0
    );

    assert(
        resources.configured_stack_bytes ==
        4096
    );
    


    /*
     * Device should still be running
     * after safe reconfiguration.
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


    /*
     * ROLLBACK
     *
     * Restore:
     *
     * period = 2000 ms
     * stack  = 2048 bytes
     */
    assert(
        uthome_runtime_rollback_device(
            "sensor"
        ) == 0
    );


    assert(
        uthome_runtime_get_resources(
            &resources
        ) == 0
    );

    assert(
        resources.configured_stack_bytes ==
        2048
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


    /*
     * Confirm the original 2000 ms
     * period was restored.
     */
    assert(
        uthome_runtime_tick(
            1999
        ) == 0
    );


    assert(
        uthome_runtime_get_status(
            "sensor",
            &status
        ) == 0
    );

    assert(status.run_count == 0);


    assert(
        uthome_runtime_tick(
            1
        ) == 0
    );


    assert(
        uthome_runtime_get_status(
            "sensor",
            &status
        ) == 0
    );

    assert(status.run_count == 1);


    /*
     * STOP
     *
     * Stopping must release the
     * prepared task resource.
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

    assert(resources.active_tasks == 0);


    assert(
        uthome_runtime_get_status(
            "sensor",
            &status
        ) == 0
    );

    assert(
        status.state ==
        UTHOME_RUNTIME_STOPPED
    );


    printf(
        "All runtime lifecycle tests passed!\n"
    );

    return 0;
}