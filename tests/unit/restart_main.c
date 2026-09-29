/**
 * @file restart_main.c
 * @brief Runtime restart regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "restart_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/runtime.h"

static runtime_device_t make_device (void);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    runtime_device_t device = make_device();

    runtime_status_t status = {0};

    runtime_resource_snapshot_t resources = {0U};

    assert(0 == runtime_init());

    assert(0 == runtime_register_device(&device));

    assert(0 == runtime_start_device("sensor"));

    /*
     * Initial state.
     */
    assert(0 == runtime_get_status("sensor", &status));

    assert(UTHOME_RUNTIME_RUNNING == status.state);

    assert(0 == status.failure_count);
    assert(0 == status.restart_count);

    /*
     * Simulate first runtime failure.
     */
    assert(0 == runtime_record_failure("sensor"));

    assert(0 == runtime_get_status("sensor", &status));

    assert(UTHOME_RUNTIME_ERROR == status.state);

    assert(1 == status.failure_count);
    assert(0 == status.restart_count);

    /*
     * Restart after failure.
     */
    assert(0 == runtime_restart_device("sensor"));

    assert(0 == runtime_get_status("sensor", &status));

    assert(UTHOME_RUNTIME_RUNNING == status.state);

    assert(1 == status.failure_count);
    assert(1 == status.restart_count);

    /*
     * Restart must still leave exactly
     * one task resource.
     */
    assert(0 == runtime_get_resources(&resources));

    assert(1 == resources.active_tasks);

    assert(2048 == resources.configured_stack_bytes);

    /*
     * Simulate another failure.
     */
    assert(0 == runtime_record_failure("sensor"));

    assert(0 == runtime_restart_device("sensor"));

    assert(0 == runtime_get_status("sensor", &status));

    assert(2 == status.failure_count);
    assert(2 == status.restart_count);

    assert(UTHOME_RUNTIME_RUNNING == status.state);

    printf("All runtime restart tests passed!\n");

    return 0;
}

/** @brief Make device. */
static runtime_device_t make_device (void)
{
    runtime_device_t device = {.period_ms        = 2000U,
                               .priority         = 3U,
                               .stack_size_bytes = 2048U,
                               .b_enabled        = true};

    strncpy(device.name, "sensor", sizeof(device.name) - 1U);

    device.name[sizeof(device.name) - 1U] = '\0';

    return device;
}

/* End of file. */

