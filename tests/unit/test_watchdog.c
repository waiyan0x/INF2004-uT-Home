#include <assert.h>
#include <stdio.h>

#include "uthome/health.h"


int main(void)
{
    uint32_t missed_count = 0;


    /*
     * Initialise health + watchdog.
     */
    assert(
        uthome_health_init() ==
        UTHOME_HEALTH_OK
    );

    assert(
        uthome_health_watchdog_init() ==
        UTHOME_HEALTH_OK
    );


    /*
     * Watch sensor_task.
     *
     * Timeout = 2000 ms.
     * Monitoring starts at 1000 ms.
     */
    assert(
        uthome_health_watch_task(
            "sensor_task",
            2000,
            1000
        ) == UTHOME_HEALTH_OK
    );


    /*
     * 1500 ms elapsed.
     * Still inside timeout.
     */
    assert(
        uthome_health_check_heartbeats(
            2500
        ) == UTHOME_HEALTH_OK
    );

    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
            &missed_count
        ) == UTHOME_HEALTH_OK
    );

    assert(missed_count == 0);


    /*
     * More than 2000 ms has now elapsed.
     *
     * Watchdog should report one missed
     * heartbeat.
     */
    assert(
        uthome_health_check_heartbeats(
            3001
        ) == UTHOME_HEALTH_OK
    );

    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
            &missed_count
        ) == UTHOME_HEALTH_OK
    );

    assert(missed_count == 1);


    /*
     * Checking repeatedly without a new
     * heartbeat must NOT create additional
     * missed events.
     */
    assert(
        uthome_health_check_heartbeats(
            4000
        ) == UTHOME_HEALTH_OK
    );

    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
            &missed_count
        ) == UTHOME_HEALTH_OK
    );

    assert(missed_count == 1);


    /*
     * Task recovers and sends heartbeat.
     */
    assert(
        uthome_health_task_heartbeat(
            "sensor_task",
            4500
        ) == UTHOME_HEALTH_OK
    );


    /*
     * Still within timeout.
     */
    assert(
        uthome_health_check_heartbeats(
            6000
        ) == UTHOME_HEALTH_OK
    );

    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
            &missed_count
        ) == UTHOME_HEALTH_OK
    );

    assert(missed_count == 1);


    /*
     * Task misses another heartbeat deadline
     * after recovering.
     */
    assert(
        uthome_health_check_heartbeats(
            6501
        ) == UTHOME_HEALTH_OK
    );

    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_TASK_HEARTBEAT_MISSED,
            &missed_count
        ) == UTHOME_HEALTH_OK
    );

    assert(missed_count == 2);


    printf(
        "All watchdog tests passed!\n"
    );

    return 0;
}