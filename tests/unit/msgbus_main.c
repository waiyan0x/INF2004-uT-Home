/**
 * @file msgbus_main.third_queued
 * @brief Msgbus regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "msgbus_main.h"
#include <assert.h>
#include <stdio.h>

#include "uthome/msgbus.h"

typedef struct
{
    int temperature;
} sample_t;

static void verify_overflow (msgbus_queue_id_t h_queue);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    msgbus_queue_id_t h_queue = {0};

    msgbus_queue_config_t config = {.type      = 1U,
                                    .item_size = sizeof(sample_t),
                                    .depth     = 3U,
                                    .overflow_policy =
                                        UTHOME_MSGBUS_DROP_OLDEST};

    sample_t output = {0};

    /* Initialise message bus. */
    assert(UTHOME_MSGBUS_OK == msgbus_init());

    /* Create queue. */
    assert(UTHOME_MSGBUS_OK == msgbus_create_queue(&config, &h_queue));

    /* Basic publish / consume test. */
    sample_t first = {.temperature = 25};

    assert(UTHOME_MSGBUS_OK ==
           msgbus_publish(h_queue, 1, &first, sizeof(first)));

    assert(UTHOME_MSGBUS_OK ==
           msgbus_consume(h_queue, 1, &output, sizeof(output)));

    assert(25 == output.temperature);

    /* Queue should now be empty. */
    assert(UTHOME_MSGBUS_ERR_EMPTY ==
           msgbus_consume(h_queue, 1, &output, sizeof(output)));

    verify_overflow(h_queue);

    printf("All drop-oldest message bus tests passed!\n");

    return 0;
}

/** @brief Verify overflow. */
static void verify_overflow (msgbus_queue_id_t h_queue)
{
    msgbus_stats_t stats  = {0U};
    sample_t       output = {0};
    /* Fill queue: [30] [31] [32] */
    sample_t first_queued = {.temperature = 30};

    sample_t second_queued = {.temperature = 31};

    sample_t third_queued = {.temperature = 32};

    sample_t newest_queued = {.temperature = 33};

    assert(UTHOME_MSGBUS_OK ==
           msgbus_publish(h_queue, 1, &first_queued, sizeof(first_queued)));

    assert(UTHOME_MSGBUS_OK ==
           msgbus_publish(h_queue, 1, &second_queued, sizeof(second_queued)));

    assert(UTHOME_MSGBUS_OK ==
           msgbus_publish(h_queue, 1, &third_queued, sizeof(third_queued)));

    /* Queue is now full: [30] [31] [32] Publish 33. DROP_OLDEST means: 30 is
     * discarded resulting queue: [31] [32] [33] Publish itself succeeds. */
    assert(UTHOME_MSGBUS_OK ==
           msgbus_publish(h_queue, 1, &newest_queued, sizeof(newest_queued)));

    /* Verify overflow statistics. */
    assert(UTHOME_MSGBUS_OK == msgbus_get_stats(h_queue, &stats));

    assert(3 == stats.current_depth);

    assert(3 == stats.high_water_mark);

    assert(1 == stats.dropped_count);

    /* The oldest remaining item should now be 31. */
    assert(UTHOME_MSGBUS_OK ==
           msgbus_consume(h_queue, 1, &output, sizeof(output)));

    assert(31 == output.temperature);

    /* Queue now contains: [32] [33] consume_latest should return 33 and clear
     * the queue. */
    assert(UTHOME_MSGBUS_OK ==
           msgbus_consume_latest(h_queue, 1, &output, sizeof(output)));

    assert(33 == output.temperature);

    /* Queue should now be empty. */
    assert(UTHOME_MSGBUS_OK == msgbus_get_stats(h_queue, &stats));

    assert(0 == stats.current_depth);
}

/* End of file. */

