/**
 * @file rtstats_main.c
 * @brief Runtime metrics regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "rtstats_main.h"
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

    assert(0 == runtime_init());

    assert(0 == runtime_register_device(&device));

    assert(0 == runtime_start_device("sensor_task"));

    /*
     * First execution establishes
     * the reference timestamp.
     */
    assert(0 == runtime_record_execution("sensor_task", 1000));

    /*
     * 3005 - 1000 = 2005
     *
     * Expected = 2000
     * Jitter = 5
     */
    assert(0 == runtime_record_execution("sensor_task", 3005));

    assert(0 == runtime_get_status("sensor_task", &status));

    assert(5 == status.last_jitter_ms);
    assert(5 == status.max_jitter_ms);

    /*
     * 4998 - 3005 = 1993
     *
     * Expected = 2000
     * Jitter = 7
     */
    assert(0 == runtime_record_execution("sensor_task", 4998));

    assert(0 == runtime_get_status("sensor_task", &status));

    assert(7 == status.last_jitter_ms);
    assert(7 == status.max_jitter_ms);

    /*
     * 6998 - 4998 = 2000
     *
     * Perfect timing.
     * Jitter = 0
     */
    assert(0 == runtime_record_execution("sensor_task", 6998));

    assert(0 == runtime_get_status("sensor_task", &status));

    assert(0 == status.last_jitter_ms);

    /*
     * Worst jitter observed remains 7.
     */
    assert(7 == status.max_jitter_ms);

    printf("All runtime timing metric tests passed!\n");

    return 0;
}

/** @brief Make device. */
static runtime_device_t make_device (void)
{
    runtime_device_t device = {.period_ms        = 2000U,
                               .priority         = 3U,
                               .stack_size_bytes = 3072U,
                               .b_enabled        = true};

    strncpy(device.name, "sensor_task", sizeof(device.name) - 1U);

    device.name[sizeof(device.name) - 1U] = '\0';

    return device;
}

/* End of file. */

