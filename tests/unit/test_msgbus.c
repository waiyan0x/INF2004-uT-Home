#include <assert.h>
#include <stdio.h>

#include "uthome/msgbus.h"


typedef struct {
    int temperature;
} sample_t;


int main(void)
{
    uthome_msgbus_queue_id_t queue_id;

    uthome_msgbus_queue_config_t config = {
        .type = 1,
        .item_size = sizeof(sample_t),
        .depth = 3,
        .overflow_policy =
            UTHOME_MSGBUS_DROP_OLDEST
    };

    uthome_msgbus_stats_t stats;
    sample_t output;


    /*
     * Initialise message bus.
     */
    assert(
        uthome_msgbus_init() ==
        UTHOME_MSGBUS_OK
    );


    /*
     * Create queue.
     */
    assert(
        uthome_msgbus_create_queue(
            &config,
            &queue_id
        ) == UTHOME_MSGBUS_OK
    );


    /*
     * Basic publish / consume test.
     */
    sample_t first = {
        .temperature = 25
    };

    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &first,
            sizeof(first)
        ) == UTHOME_MSGBUS_OK
    );


    assert(
        uthome_msgbus_consume(
            queue_id,
            1,
            &output,
            sizeof(output)
        ) == UTHOME_MSGBUS_OK
    );

    assert(
        output.temperature == 25
    );


    /*
     * Queue should now be empty.
     */
    assert(
        uthome_msgbus_consume(
            queue_id,
            1,
            &output,
            sizeof(output)
        ) == UTHOME_MSGBUS_ERR_EMPTY
    );


    /*
     * Fill queue:
     *
     * [30] [31] [32]
     */
    sample_t a = {
        .temperature = 30
    };

    sample_t b = {
        .temperature = 31
    };

    sample_t c = {
        .temperature = 32
    };

    sample_t d = {
        .temperature = 33
    };


    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &a,
            sizeof(a)
        ) == UTHOME_MSGBUS_OK
    );

    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &b,
            sizeof(b)
        ) == UTHOME_MSGBUS_OK
    );

    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &c,
            sizeof(c)
        ) == UTHOME_MSGBUS_OK
    );


    /*
     * Queue is now full:
     *
     * [30] [31] [32]
     *
     * Publish 33.
     *
     * DROP_OLDEST means:
     *
     * 30 is discarded
     *
     * resulting queue:
     *
     * [31] [32] [33]
     *
     * Publish itself succeeds.
     */
    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &d,
            sizeof(d)
        ) == UTHOME_MSGBUS_OK
    );


    /*
     * Verify overflow statistics.
     */
    assert(
        uthome_msgbus_get_stats(
            queue_id,
            &stats
        ) == UTHOME_MSGBUS_OK
    );

    assert(
        stats.current_depth == 3
    );

    assert(
        stats.high_water_mark == 3
    );

    assert(
        stats.dropped_count == 1
    );


    /*
     * The oldest remaining item
     * should now be 31.
     */
    assert(
        uthome_msgbus_consume(
            queue_id,
            1,
            &output,
            sizeof(output)
        ) == UTHOME_MSGBUS_OK
    );

    assert(
        output.temperature == 31
    );


    /*
     * Queue now contains:
     *
     * [32] [33]
     *
     * consume_latest should return 33
     * and clear the queue.
     */
    assert(
        uthome_msgbus_consume_latest(
            queue_id,
            1,
            &output,
            sizeof(output)
        ) == UTHOME_MSGBUS_OK
    );

    assert(
        output.temperature == 33
    );


    /*
     * Queue should now be empty.
     */
    assert(
        uthome_msgbus_get_stats(
            queue_id,
            &stats
        ) == UTHOME_MSGBUS_OK
    );

    assert(
        stats.current_depth == 0
    );


    printf(
        "All drop-oldest message bus tests passed!\n"
    );

    return 0;
}