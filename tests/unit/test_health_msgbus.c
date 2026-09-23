#include <assert.h>
#include <stdio.h>

#include "uthome/health.h"
#include "uthome/msgbus.h"


typedef struct {
    int value;
} test_message_t;


int main(void)
{
    uthome_msgbus_queue_id_t queue_id;

    uthome_msgbus_queue_config_t config = {
        .type = 1,
        .item_size = sizeof(test_message_t),
        .depth = 2,

        /*
         * When the queue is full,
         * discard the oldest item and
         * keep the newest data.
         */
        .overflow_policy =
            UTHOME_MSGBUS_DROP_OLDEST
    };

    uint32_t previous_drop_count = 0;
    uint32_t health_count = 0;

    uthome_msgbus_stats_t stats;


    /*
     * Initialise both subsystems.
     */
    assert(
        uthome_msgbus_init() ==
        UTHOME_MSGBUS_OK
    );

    assert(
        uthome_health_init() ==
        UTHOME_HEALTH_OK
    );


    /*
     * Create a queue with only two slots.
     */
    assert(
        uthome_msgbus_create_queue(
            &config,
            &queue_id
        ) == UTHOME_MSGBUS_OK
    );


    test_message_t message1 = {
        .value = 10
    };

    test_message_t message2 = {
        .value = 20
    };

    test_message_t message3 = {
        .value = 30
    };

    test_message_t message4 = {
        .value = 40
    };


    /*
     * Fill the queue.
     *
     * Queue:
     *
     * [10] [20]
     */
    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &message1,
            sizeof(message1)
        ) == UTHOME_MSGBUS_OK
    );

    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &message2,
            sizeof(message2)
        ) == UTHOME_MSGBUS_OK
    );


    /*
     * Queue is full.
     *
     * Publishing 30 causes:
     *
     * [10] [20]
     *
     *      ↓
     *
     * drop 10
     *
     *      ↓
     *
     * [20] [30]
     *
     * The publish itself succeeds because
     * DROP_OLDEST makes room for the new item.
     */
    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &message3,
            sizeof(message3)
        ) == UTHOME_MSGBUS_OK
    );


    /*
     * Verify that one item was dropped.
     */
    assert(
        uthome_msgbus_get_stats(
            queue_id,
            &stats
        ) == UTHOME_MSGBUS_OK
    );

    assert(stats.dropped_count == 1);
    assert(stats.current_depth == 2);


    /*
     * Health checks the queue statistics.
     *
     * Because dropped_count changed from
     * 0 to 1, health should record one
     * QUEUE_OVERFLOW event.
     */
    assert(
        uthome_health_check_queue(
            queue_id,
            1000,
            &previous_drop_count
        ) == UTHOME_HEALTH_OK
    );


    assert(
        previous_drop_count == 1
    );


    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_QUEUE_OVERFLOW,
            &health_count
        ) == UTHOME_HEALTH_OK
    );

    assert(health_count == 1);


    /*
     * Checking again without another queue
     * overflow must NOT generate another
     * health event.
     */
    assert(
        uthome_health_check_queue(
            queue_id,
            1100,
            &previous_drop_count
        ) == UTHOME_HEALTH_OK
    );


    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_QUEUE_OVERFLOW,
            &health_count
        ) == UTHOME_HEALTH_OK
    );

    assert(health_count == 1);


    /*
     * Queue is still full:
     *
     * [20] [30]
     *
     * Publish 40:
     *
     * drop 20
     *
     * resulting queue:
     *
     * [30] [40]
     */
    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &message4,
            sizeof(message4)
        ) == UTHOME_MSGBUS_OK
    );


    /*
     * There should now be two dropped items.
     */
    assert(
        uthome_msgbus_get_stats(
            queue_id,
            &stats
        ) == UTHOME_MSGBUS_OK
    );

    assert(stats.dropped_count == 2);
    assert(stats.current_depth == 2);


    /*
     * Health detects the second overflow.
     *
     * However, this occurs only 200 ms after
     * the first emitted overflow event.
     *
     * Therefore:
     *
     * - the health event is still counted
     * - output is rate-limited
     */
    assert(
        uthome_health_check_queue(
            queue_id,
            1200,
            &previous_drop_count
        ) == UTHOME_HEALTH_RATE_LIMITED
    );


    assert(
        previous_drop_count == 2
    );


    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_QUEUE_OVERFLOW,
            &health_count
        ) == UTHOME_HEALTH_OK
    );

    assert(health_count == 2);


    /*
     * Final statistics check.
     */
    assert(
        uthome_msgbus_get_stats(
            queue_id,
            &stats
        ) == UTHOME_MSGBUS_OK
    );

    assert(stats.dropped_count == 2);
    assert(stats.high_water_mark == 2);


    printf(
        "Health and drop-oldest message bus integration tests passed!\n"
    );

    return 0;
}