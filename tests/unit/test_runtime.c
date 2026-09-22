#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/runtime.h"


static uthome_runtime_device_t make_device(
    const char *id,
    bool enabled)
{
    uthome_runtime_device_t device = {
        .period_ms = 2000,
        .priority = 3,
        .stack_size_bytes = 3072,
        .enabled = enabled
    };

    /*
     * Copy the device ID safely.
     */
    strncpy(
        device.id,
        id,
        sizeof(device.id) - 1
    );

    /*
     * Guarantee null termination.
     */
    device.id[sizeof(device.id) - 1] = '\0';

    return device;
}


int main(void)
{
    uthome_runtime_status_t status;
    uthome_runtime_device_t device;


    /*
     * TEST 1:
     * Initialise runtime.
     */
    assert(uthome_runtime_init() == 0);

    assert(
        uthome_runtime_device_count() == 0
    );


    /*
     * TEST 2:
     * Register an enabled device.
     */
    device = make_device(
        "environment_sensor",
        true
    );

    assert(
        uthome_runtime_register_device(
            &device
        ) == 0
    );

    assert(
        uthome_runtime_get_status(
            "environment_sensor",
            &status
        ) == 0
    );

    assert(
        status.state ==
        UTHOME_RUNTIME_READY
    );


    /*
     * TEST 3:
     * Start the device.
     */
    assert(
        uthome_runtime_start_device(
            "environment_sensor"
        ) == 0
    );

    assert(
        uthome_runtime_get_status(
            "environment_sensor",
            &status
        ) == 0
    );

    assert(
        status.state ==
        UTHOME_RUNTIME_RUNNING
    );


    /*
     * TEST 4:
     * Update the running device.
     *
     * Change its period from
     * 2000 ms to 5000 ms.
     */
    device.period_ms = 5000;

    assert(
        uthome_runtime_update_device(
            "environment_sensor",
            &device
        ) == 0
    );

    assert(
        uthome_runtime_get_status(
            "environment_sensor",
            &status
        ) == 0
    );

    /*
     * Updating a running device
     * should keep it running.
     */
    assert(
        status.state ==
        UTHOME_RUNTIME_RUNNING
    );


    /*
     * TEST 5:
     * Disable the running device.
     *
     * This should stop the device
     * and release its task slot.
     */
    device.enabled = false;

    assert(
        uthome_runtime_update_device(
            "environment_sensor",
            &device
        ) == 0
    );

    assert(
        uthome_runtime_get_status(
            "environment_sensor",
            &status
        ) == 0
    );

    assert(
        status.state ==
        UTHOME_RUNTIME_STOPPED
    );


    /*
     * A disabled device must not start.
     */
    assert(
        uthome_runtime_start_device(
            "environment_sensor"
        ) == -2
    );


    /*
     * TEST 6:
     * Fill every available runtime
     * task slot.
     */
    for (
        size_t i = 0;
        i < UTHOME_RUNTIME_MAX_DEVICE_TASKS;
        ++i
    ) {
        char id[UTHOME_DEVICE_ID_MAX];

        snprintf(
            id,
            sizeof(id),
            "device_%zu",
            i
        );

        device = make_device(
            id,
            true
        );

        assert(
            uthome_runtime_register_device(
                &device
            ) == 0
        );

        assert(
            uthome_runtime_start_device(
                id
            ) == 0
        );
    }


    /*
     * TEST 7:
     * Register one additional device.
     *
     * Registration is allowed,
     * but starting it should fail
     * because every task slot is occupied.
     */
    device = make_device(
        "overflow",
        true
    );

    assert(
        uthome_runtime_register_device(
            &device
        ) == 0
    );

    assert(
        uthome_runtime_start_device(
            "overflow"
        ) == -3
    );


    /*
     * TEST 8:
     * Stop one existing device.
     *
     * This should release one task slot.
     */
    assert(
        uthome_runtime_stop_device(
            "device_0"
        ) == 0
    );


    /*
     * The previously rejected device
     * should now be able to start using
     * the released task slot.
     */
    assert(
        uthome_runtime_start_device(
            "overflow"
        ) == 0
    );

    assert(
        uthome_runtime_get_status(
            "overflow",
            &status
        ) == 0
    );

    assert(
        status.state ==
        UTHOME_RUNTIME_RUNNING
    );


    printf(
        "All runtime tests passed!\n"
    );

    return 0;
}