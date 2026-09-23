#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/runtime.h"


static uthome_runtime_device_t make_device(void)
{
    uthome_runtime_device_t device = {
        .period_ms = 2000,
        .priority = 3,
        .stack_size_bytes = 3072,
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
    uthome_runtime_device_t device =
        make_device();

    uthome_runtime_status_t status;


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
            "sensor_task"
        ) == 0
    );


    /*
     * First execution establishes
     * the reference timestamp.
     */
    assert(
        uthome_runtime_record_execution(
            "sensor_task",
            1000
        ) == 0
    );


    /*
     * 3005 - 1000 = 2005
     *
     * Expected = 2000
     * Jitter = 5
     */
    assert(
        uthome_runtime_record_execution(
            "sensor_task",
            3005
        ) == 0
    );

    assert(
        uthome_runtime_get_status(
            "sensor_task",
            &status
        ) == 0
    );

    assert(status.last_jitter_ms == 5);
    assert(status.max_jitter_ms == 5);


    /*
     * 4998 - 3005 = 1993
     *
     * Expected = 2000
     * Jitter = 7
     */
    assert(
        uthome_runtime_record_execution(
            "sensor_task",
            4998
        ) == 0
    );

    assert(
        uthome_runtime_get_status(
            "sensor_task",
            &status
        ) == 0
    );

    assert(status.last_jitter_ms == 7);
    assert(status.max_jitter_ms == 7);


    /*
     * 6998 - 4998 = 2000
     *
     * Perfect timing.
     * Jitter = 0
     */
    assert(
        uthome_runtime_record_execution(
            "sensor_task",
            6998
        ) == 0
    );

    assert(
        uthome_runtime_get_status(
            "sensor_task",
            &status
        ) == 0
    );

    assert(status.last_jitter_ms == 0);

    /*
     * Worst jitter observed remains 7.
     */
    assert(status.max_jitter_ms == 7);


    printf(
        "All runtime timing metric tests passed!\n"
    );

    return 0;
}
