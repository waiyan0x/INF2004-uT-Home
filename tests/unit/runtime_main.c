/**
 * @file runtime_main.c
 * @brief Runtime regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "runtime_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/runtime.h"

static runtime_device_t make_device (const char * p_id, bool b_enabled);
static void verify_capacity (void);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    runtime_status_t status = {0};
    runtime_device_t device = {0};

    /* TEST 1: Initialise runtime. */
    assert(0 == runtime_init());

    assert(0 == runtime_device_count());

    /* TEST 2: Register an enabled device. */
    device = make_device("environment_sensor", true);

    assert(0 == runtime_register_device(&device));

    assert(0 == runtime_get_status("environment_sensor", &status));

    assert(UTHOME_RUNTIME_READY == status.state);

    /* TEST 3: Start the device. */
    assert(0 == runtime_start_device("environment_sensor"));

    assert(0 == runtime_get_status("environment_sensor", &status));

    assert(UTHOME_RUNTIME_RUNNING == status.state);

    /* TEST 4: Update the running device. Change its period from 2000 ms to 5000
     * ms. */
    device.period_ms = 5000U;

    assert(0 == runtime_update_device("environment_sensor", &device));

    assert(0 == runtime_get_status("environment_sensor", &status));

    /* Updating a running device should keep it running. */
    assert(UTHOME_RUNTIME_RUNNING == status.state);

    /* TEST 5: Disable the running device. This should stop the device and
     * release its task slot. */
    device.b_enabled = false;

    assert(0 == runtime_update_device("environment_sensor", &device));

    assert(0 == runtime_get_status("environment_sensor", &status));

    assert(UTHOME_RUNTIME_STOPPED == status.state);

    /* A disabled device must not start. */
    assert(-2 == runtime_start_device("environment_sensor"));

    verify_capacity();

    printf("All runtime tests passed!\n");

    return 0;
}

/** @brief Make device. */
static runtime_device_t make_device (const char * p_id, bool b_enabled)
{
    runtime_device_t device = {.period_ms        = 2000U,
                               .priority         = 3U,
                               .stack_size_bytes = 3072U,
                               .b_enabled        = b_enabled};

    /* Copy the device ID safely. */
    strncpy(device.name, p_id, sizeof(device.name) - 1U);

    /* Guarantee null termination. */
    device.name[sizeof(device.name) - 1U] = '\0';

    return device;
}

/** @brief Verify capacity. */
static void verify_capacity (void)
{
    runtime_status_t status = {0};
    runtime_device_t device = {0};
    /* TEST 6: Fill every available runtime task slot. */
    for (size_t index = 0U; index < UTHOME_RUNTIME_MAX_DEVICE_TASKS; ++index)
    {
        char name[UTHOME_DEVICE_ID_MAX];

        snprintf(name, sizeof(name), "device_%zu", index);

        device = make_device(name, true);

        assert(0 == runtime_register_device(&device));

        assert(0 == runtime_start_device(name));
    }

    /* TEST 7: Register one additional device. Registration is allowed, but
     * starting it should fail because every task slot is occupied. */
    device = make_device("overflow", true);

    assert(0 == runtime_register_device(&device));

    assert(-3 == runtime_start_device("overflow"));

    /* TEST 8: Stop one existing device. This should release one task slot. */
    assert(0 == runtime_stop_device("device_0"));

    /* The previously rejected device should now be able to start using the
     * released task slot. */
    assert(0 == runtime_start_device("overflow"));

    assert(0 == runtime_get_status("overflow", &status));

    assert(UTHOME_RUNTIME_RUNNING == status.state);
}

/* End of file. */

