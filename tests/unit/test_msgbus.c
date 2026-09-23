#include <assert.h>
#include <stdio.h>

#include "uthome/msgbus.h"


typedef struct {
    int temperature;
} sample_t;


int main(void)
{
    uthome_msgbus_queue_id_t queue_id;
    uthome_msgbus_stats_t stats;

    uthome_msgbus_queue_config_t config = {
        .type = 1,
        .item_size = sizeof(sample_t),
        .depth = 3
    };


    /*
     * TEST 1:
     * Initialise message bus.
     */
    assert(
        uthome_msgbus_init() ==
        UTHOME_MSGBUS_OK
    );


    /*
     * TEST 2:
     * Create bounded queue.
     */
    assert(
        uthome_msgbus_create_queue(
            &config,
            &queue_id
        ) == UTHOME_MSGBUS_OK
    );


    /*
     * TEST 3:
     * Publish one item.
     */
    sample_t sample = {
        .temperature = 25
    };

    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &sample,
            sizeof(sample)
        ) == UTHOME_MSGBUS_OK
    );


    /*
     * TEST 4:
     * Consume it.
     */
    sample_t output = {0};

    assert(
        uthome_msgbus_consume(
            queue_id,
            1,
            &output,
            sizeof(output)
        ) == UTHOME_MSGBUS_OK
    );

    assert(output.temperature == 25);


    /*
     * TEST 5:
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
     * TEST 6:
     * Fill the bounded queue.
     */
    sample.temperature = 30;

    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &sample,
            sizeof(sample)
        ) == UTHOME_MSGBUS_OK
    );

    sample.temperature = 31;

    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &sample,
            sizeof(sample)
        ) == UTHOME_MSGBUS_OK
    );

    sample.temperature = 32;

    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &sample,
            sizeof(sample)
        ) == UTHOME_MSGBUS_OK
    );


    /*
     * TEST 7:
     * Fourth item must fail because
     * queue depth is only three.
     */
    sample.temperature = 33;

    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &sample,
            sizeof(sample)
        ) == UTHOME_MSGBUS_ERR_FULL
    );


    /*
     * TEST 8:
     * Verify health statistics.
     */
    assert(
        uthome_msgbus_get_stats(
            queue_id,
            &stats
        ) == UTHOME_MSGBUS_OK
    );

    assert(stats.current_depth == 3);
    assert(stats.high_water_mark == 3);
    assert(stats.dropped_count == 1);


    /*
     * TEST 9:
     * Request newest sample.
     *
     * Should return temperature 32
     * and clear older queued samples.
     */
    assert(
        uthome_msgbus_consume_latest(
            queue_id,
            1,
            &output,
            sizeof(output)
        ) == UTHOME_MSGBUS_OK
    );

    assert(output.temperature == 32);


    assert(
        uthome_msgbus_get_stats(
            queue_id,
            &stats
        ) == UTHOME_MSGBUS_OK
    );

    assert(stats.current_depth == 0);


    printf(
        "All message bus tests passed!\n"
    );

    return 0;
}