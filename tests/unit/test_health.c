#include <assert.h>
#include <stdio.h>

#include "uthome/health.h"


int main(void)
{
    uthome_health_snapshot_t snapshot;
    uint32_t count;


    /*
     * TEST 1:
     * Initialise health monitoring.
     */
    assert(
        uthome_health_init() ==
        UTHOME_HEALTH_OK
    );


    /*
     * TEST 2:
     * First queue-overflow event.
     *
     * First occurrence should be emitted.
     */
    assert(
        uthome_health_report(
            UTHOME_HEALTH_QUEUE_OVERFLOW,
            1000
        ) == UTHOME_HEALTH_OK
    );


    /*
     * TEST 3:
     * Same fault 200 ms later.
     *
     * It must still be counted, but should
     * be rate-limited for output.
     */
    assert(
        uthome_health_report(
            UTHOME_HEALTH_QUEUE_OVERFLOW,
            1200
        ) == UTHOME_HEALTH_RATE_LIMITED
    );


    /*
     * TEST 4:
     * Another occurrence within the
     * same rate-limit window.
     */
    assert(
        uthome_health_report(
            UTHOME_HEALTH_QUEUE_OVERFLOW,
            1500
        ) == UTHOME_HEALTH_RATE_LIMITED
    );


    /*
     * All three occurrences must have
     * been counted.
     */
    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_QUEUE_OVERFLOW,
            &count
        ) == UTHOME_HEALTH_OK
    );

    assert(count == 3);


    /*
     * TEST 5:
     * Once 1000 ms has passed since the
     * last emitted event, output is allowed.
     */
    assert(
        uthome_health_report(
            UTHOME_HEALTH_QUEUE_OVERFLOW,
            2000
        ) == UTHOME_HEALTH_OK
    );


    /*
     * TEST 6:
     * Different event type has its own
     * independent rate limiter.
     */
    assert(
        uthome_health_report(
            UTHOME_HEALTH_DEVICE_TIMEOUT,
            2050
        ) == UTHOME_HEALTH_OK
    );


    /*
     * TEST 7:
     * Read the health snapshot.
     */
    assert(
        uthome_health_get_snapshot(
            &snapshot
        ) == UTHOME_HEALTH_OK
    );

    assert(snapshot.total_events == 5);

    assert(
        snapshot.event_count[
            UTHOME_HEALTH_QUEUE_OVERFLOW
        ] == 4
    );

    assert(
        snapshot.event_count[
            UTHOME_HEALTH_DEVICE_TIMEOUT
        ] == 1
    );

    assert(
        snapshot.suppressed_count[
            UTHOME_HEALTH_QUEUE_OVERFLOW
        ] == 2
    );

    assert(snapshot.total_suppressed == 2);

    assert(
        snapshot.last_event ==
        UTHOME_HEALTH_DEVICE_TIMEOUT
    );

    assert(snapshot.last_event_time_ms == 2050);


    printf(
        "All health monitoring tests passed!\n"
    );

    return 0;
}