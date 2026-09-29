/**
 * @file rtfault_main.c
 * @brief Runtime failure regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "rtfault_main.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "uthome/watchdog.h"
#include "uthome/runtime.h"

static runtime_device_t make_device (void);
static void verify_recovery (void);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    uint32_t heartbeat_missed_count = 0U;

    /* Initialise runtime + health. */
    assert(0 == runtime_init());

    assert(UTHOME_HEALTH_OK == health_init());

    assert(UTHOME_HEALTH_OK == watchdog_init());

    /* Register and start sensor task. */
    runtime_device_t device = make_device();

    assert(0 == runtime_register_device(&device));

    assert(0 == runtime_start_device("sensor_task"));

    /* Watch heartbeat. Task must heartbeat at least once every 3000 ms. */
    assert(UTHOME_HEALTH_OK == watchdog_watch_task("sensor_task", 3000, 0));

    /* Task works normally first. */
    assert(UTHOME_HEALTH_OK == watchdog_heartbeat("sensor_task", 1000));

    /* 2500 ms after heartbeat: still healthy. */
    assert(UTHOME_HEALTH_OK == watchdog_check(3500));

    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
                                  &heartbeat_missed_count));

    assert(0 == heartbeat_missed_count);

    /* FAULT INJECTION: Pretend the sensor task stops responding. At 4001 ms
     * after the last heartbeat, watchdog must detect a failure. */
    assert(UTHOME_HEALTH_OK == watchdog_check(4001));

    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
                                  &heartbeat_missed_count));

    assert(1 == heartbeat_missed_count);

    verify_recovery();

    printf("Runtime fault-injection recovery test passed!\n");

    return 0;
}

/** @brief Make device. */
static runtime_device_t make_device (void)
{
    runtime_device_t device = {.period_ms        = 2000U,
                               .priority         = 3U,
                               .stack_size_bytes = 2048U,
                               .b_enabled        = true};

    strncpy(device.name, "sensor_task", sizeof(device.name) - 1U);

    device.name[sizeof(device.name) - 1U] = '\0';

    return device;
}

/** @brief Verify recovery. */
static void verify_recovery (void)
{
    runtime_status_t            status                 = {0};
    runtime_resource_snapshot_t resources              = {0U};
    uint32_t                    heartbeat_missed_count = 0U;
    /* Runtime records the corresponding task failure. */
    assert(0 == runtime_record_failure("sensor_task"));

    assert(0 == runtime_get_status("sensor_task", &status));

    assert(UTHOME_RUNTIME_ERROR == status.state);

    assert(1 == status.failure_count);

    /* Recovery: Restart the failed device. */
    assert(0 == runtime_restart_device("sensor_task"));

    assert(0 == runtime_get_status("sensor_task", &status));

    assert(UTHOME_RUNTIME_RUNNING == status.state);

    assert(1 == status.failure_count);
    assert(1 == status.restart_count);

    /* Recovery must not leak task slots. */
    assert(0 == runtime_get_resources(&resources));

    assert(1 == resources.active_tasks);

    assert(2048 == resources.configured_stack_bytes);

    /* Task resumes heartbeats after recovery. */
    assert(UTHOME_HEALTH_OK == watchdog_heartbeat("sensor_task", 5000));

    assert(UTHOME_HEALTH_OK == watchdog_check(7000));

    /* No second missed-heartbeat event. */
    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
                                  &heartbeat_missed_count));

    assert(1 == heartbeat_missed_count);
}

/* End of file. */

