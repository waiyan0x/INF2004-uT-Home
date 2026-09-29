/**
 * @file rtlife_main.c
 * @brief Runtime lifecycle regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "rtlife_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/runtime.h"

static runtime_device_t
make_device (const char * p_id, uint32_t period_ms, size_t stack_size);
static void verify_reconfiguration (const runtime_device_t * p_sensor);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    runtime_resource_snapshot_t resources = {0U};
    runtime_status_t            status    = {0};

    runtime_device_t sensor = make_device("sensor", 2000U, 2048U);

    assert(0 == runtime_init());

    /* Register device. Registration should not allocate a task resource yet. */
    assert(0 == runtime_register_device(&sensor));

    assert(0 == runtime_get_resources(&resources));

    assert(0 == resources.active_tasks);

    /* PREPARE Reserve one task slot but do not start execution. */
    assert(0 == runtime_prepare_device("sensor"));

    assert(0 == runtime_get_resources(&resources));

    assert(1 == resources.active_tasks);

    assert(2048 == resources.configured_stack_bytes);

    assert(0 == runtime_get_status("sensor", &status));

    assert(UTHOME_RUNTIME_READY == status.state);

    /* START */
    assert(0 == runtime_start_device("sensor"));

    assert(0 == runtime_get_status("sensor", &status));

    assert(UTHOME_RUNTIME_RUNNING == status.state);

    verify_reconfiguration(&sensor);

    printf("All runtime lifecycle tests passed!\n");

    return 0;
}

/** @brief Make device. */
static runtime_device_t
make_device (const char * p_id, uint32_t period_ms, size_t stack_size)
{
    runtime_device_t device = {.period_ms        = period_ms,
                               .priority         = 3U,
                               .stack_size_bytes = stack_size,
                               .b_enabled        = true};

    strncpy(device.name, p_id, sizeof(device.name) - 1U);

    device.name[sizeof(device.name) - 1U] = '\0';

    return device;
}

/** @brief Verify reconfiguration. */
static void verify_reconfiguration (const runtime_device_t * p_sensor)
{
    runtime_resource_snapshot_t resources = {0U};
    runtime_status_t            status    = {0};
    /* RECONFIGURE Old: period = 2000 ms stack = 2048 bytes New: period = 5000
     * ms stack = 4096 bytes */
    runtime_device_t updated = *p_sensor;

    updated.period_ms        = 5000U;
    updated.stack_size_bytes = 4096U;

    assert(0 == runtime_reconfigure_device("sensor", &updated));

    assert(0 == runtime_get_resources(&resources));

    assert(4096 == resources.configured_stack_bytes);

    /* Device should still be running after safe reconfiguration. */
    assert(0 == runtime_get_status("sensor", &status));

    assert(UTHOME_RUNTIME_RUNNING == status.state);

    /* ROLLBACK Restore: period = 2000 ms stack = 2048 bytes */
    assert(0 == runtime_rollback_device("sensor"));

    assert(0 == runtime_get_resources(&resources));

    assert(2048 == resources.configured_stack_bytes);

    assert(0 == runtime_get_status("sensor", &status));

    assert(UTHOME_RUNTIME_RUNNING == status.state);

    /* Confirm the original 2000 ms period was restored. */
    assert(0 == runtime_tick(1999));

    assert(0 == runtime_get_status("sensor", &status));

    assert(0 == status.run_count);

    assert(0 == runtime_tick(1));

    assert(0 == runtime_get_status("sensor", &status));

    assert(1 == status.run_count);

    /* STOP Stopping must release the prepared task resource. */
    assert(0 == runtime_stop_device("sensor"));

    assert(0 == runtime_get_resources(&resources));

    assert(0 == resources.active_tasks);

    assert(0 == runtime_get_status("sensor", &status));

    assert(UTHOME_RUNTIME_STOPPED == status.state);
}

/* End of file. */

