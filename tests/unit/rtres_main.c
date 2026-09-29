/**
 * @file rtres_main.c
 * @brief Runtime resources regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "rtres_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/runtime.h"

static runtime_device_t make_device (const char * p_id, size_t stack_size);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    runtime_resource_snapshot_t resources = {0U};

    runtime_device_t sensor = make_device("sensor", 2048U);

    runtime_device_t actuator = make_device("actuator", 3072U);

    /*
     * Initialise runtime.
     */
    assert(0 == runtime_init());

    /*
     * Initially:
     *
     * no devices
     * no active tasks
     * no stack allocated
     */
    assert(0 == runtime_get_resources(&resources));

    assert(0 == resources.registered_devices);
    assert(0 == resources.active_tasks);

    assert(UTHOME_RUNTIME_MAX_DEVICE_TASKS == resources.free_task_slots);

    assert(0 == resources.configured_stack_bytes);

    /*
     * Register two devices.
     */
    assert(0 == runtime_register_device(&sensor));

    assert(0 == runtime_register_device(&actuator));

    /*
     * Registration alone should not
     * allocate task resources.
     */
    assert(0 == runtime_get_resources(&resources));

    assert(2 == resources.registered_devices);
    assert(0 == resources.active_tasks);

    /*
     * Start sensor.
     *
     * Sensor requires 2048 bytes.
     */
    assert(0 == runtime_start_device("sensor"));

    assert(0 == runtime_get_resources(&resources));

    assert(1 == resources.active_tasks);

    assert(2048 == resources.configured_stack_bytes);

    /*
     * Start actuator.
     *
     * Total stack:
     *
     * 2048 + 3072 = 5120
     */
    assert(0 == runtime_start_device("actuator"));

    assert(0 == runtime_get_resources(&resources));

    assert(2 == resources.active_tasks);

    assert(5120 == resources.configured_stack_bytes);

    /*
     * Stop sensor.
     *
     * Only actuator remains:
     * 3072 bytes.
     */
    assert(0 == runtime_stop_device("sensor"));

    assert(0 == runtime_get_resources(&resources));

    assert(1 == resources.active_tasks);

    assert(3072 == resources.configured_stack_bytes);

    printf("All runtime resource tests passed!\n");

    return 0;
}

/** @brief Make device. */
static runtime_device_t make_device (const char * p_id, size_t stack_size)
{
    runtime_device_t device = {.period_ms        = 2000U,
                               .priority         = 3U,
                               .stack_size_bytes = stack_size,
                               .b_enabled        = true};

    strncpy(device.name, p_id, sizeof(device.name) - 1U);

    device.name[sizeof(device.name) - 1U] = '\0';

    return device;
}

/* End of file. */

