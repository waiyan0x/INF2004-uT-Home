/**
 * @file watch_main.c
 * @brief Watchdog regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "watch_main.h"
#include <assert.h>
#include <stdio.h>

#include "uthome/watchdog.h"

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    uint32_t missed_count = 0U;

    /* Reject an ID that cannot be watched under its original identity. */
    assert(UTHOME_HEALTH_OK == watchdog_init());
    assert(UTHOME_HEALTH_ERR_INVALID ==
           watchdog_watch_task("12345678901234567890123456789012", 100, 0));
    assert(UTHOME_HEALTH_OK ==
           watchdog_watch_task("1234567890123456789012345678901", 100, 0));
    assert(UTHOME_HEALTH_OK ==
           watchdog_heartbeat("1234567890123456789012345678901", 10));

    /*
     * Initialise health + watchdog.
     */
    assert(UTHOME_HEALTH_OK == health_init());

    assert(UTHOME_HEALTH_OK == watchdog_init());

    /*
     * Watch sensor_task.
     *
     * Timeout = 2000 ms.
     * Monitoring starts at 1000 ms.
     */
    assert(UTHOME_HEALTH_OK == watchdog_watch_task("sensor_task", 2000, 1000));

    /*
     * 1500 ms elapsed.
     * Still inside timeout.
     */
    assert(UTHOME_HEALTH_OK == watchdog_check(2500));

    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
                                  &missed_count));

    assert(0 == missed_count);

    /*
     * More than 2000 ms has now elapsed.
     *
     * Watchdog should report one missed
     * heartbeat.
     */
    assert(UTHOME_HEALTH_OK == watchdog_check(3001));

    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
                                  &missed_count));

    assert(1 == missed_count);

    /*
     * Checking repeatedly without a new
     * heartbeat must NOT create additional
     * missed events.
     */
    assert(UTHOME_HEALTH_OK == watchdog_check(4000));

    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
                                  &missed_count));

    assert(1 == missed_count);

    /*
     * Task recovers and sends heartbeat.
     */
    assert(UTHOME_HEALTH_OK == watchdog_heartbeat("sensor_task", 4500));

    /*
     * Still within timeout.
     */
    assert(UTHOME_HEALTH_OK == watchdog_check(6000));

    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
                                  &missed_count));

    assert(1 == missed_count);

    /*
     * Task misses another heartbeat deadline
     * after recovering.
     */
    assert(UTHOME_HEALTH_OK == watchdog_check(6501));

    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
                                  &missed_count));

    assert(2 == missed_count);

    printf("All watchdog tests passed!\n");

    return 0;
}

/* End of file. */

