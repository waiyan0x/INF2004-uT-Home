/**
 * @file health_main.c
 * @brief Health regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "health_main.h"
#include <assert.h>
#include <stdio.h>

#include "uthome/health.h"

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    health_snapshot_t snapshot = {0U};
    uint32_t          count    = {0U};

    /*
     * TEST 1:
     * Initialise health monitoring.
     */
    assert(UTHOME_HEALTH_OK == health_init());

    /*
     * TEST 2:
     * First queue-overflow event.
     *
     * First occurrence should be emitted.
     */
    assert(UTHOME_HEALTH_OK ==
           health_report(UTHOME_HEALTH_QUEUE_OVERFLOW, 1000));

    /*
     * TEST 3:
     * Same fault 200 ms later.
     *
     * It must still be counted, but should
     * be rate-limited for output.
     */
    assert(UTHOME_HEALTH_RATE_LIMITED ==
           health_report(UTHOME_HEALTH_QUEUE_OVERFLOW, 1200));

    /*
     * TEST 4:
     * Another occurrence within the
     * same rate-limit window.
     */
    assert(UTHOME_HEALTH_RATE_LIMITED ==
           health_report(UTHOME_HEALTH_QUEUE_OVERFLOW, 1500));

    /*
     * All three occurrences must have
     * been counted.
     */
    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_QUEUE_OVERFLOW, &count));

    assert(3 == count);

    /*
     * TEST 5:
     * Once 1000 ms has passed since the
     * last emitted event, output is allowed.
     */
    assert(UTHOME_HEALTH_OK ==
           health_report(UTHOME_HEALTH_QUEUE_OVERFLOW, 2000));

    /*
     * TEST 6:
     * Different event type has its own
     * independent rate limiter.
     */
    assert(UTHOME_HEALTH_OK ==
           health_report(UTHOME_HEALTH_DEVICE_TIMEOUT, 2050));

    /*
     * TEST 7:
     * Read the health snapshot.
     */
    assert(UTHOME_HEALTH_OK == health_get_snapshot(&snapshot));

    assert(5 == snapshot.total_events);

    assert(4 == snapshot.event_count[UTHOME_HEALTH_QUEUE_OVERFLOW]);

    assert(1 == snapshot.event_count[UTHOME_HEALTH_DEVICE_TIMEOUT]);

    assert(2 == snapshot.suppressed_count[UTHOME_HEALTH_QUEUE_OVERFLOW]);

    assert(2 == snapshot.total_suppressed);

    assert(UTHOME_HEALTH_DEVICE_TIMEOUT == snapshot.last_event);

    assert(2050 == snapshot.last_event_time_ms);

    printf("All health monitoring tests passed!\n");

    return 0;
}

/* End of file. */

