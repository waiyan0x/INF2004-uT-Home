/**
 * @file hlthbus_main.c
 * @brief Health msgbus regression coverage.
 * @details Project-owned host test; assertions execute required test actions.
 */
#ifdef NDEBUG
#error "These tests require assertions to remain enabled."
#endif
#include "hlthbus_main.h"
#include <assert.h>
#include <stdio.h>

#include "uthome/hlthbus.h"
#include "uthome/msgbus.h"

typedef struct
{
    int value;
} test_message_t;

static void verify_second_drop (msgbus_queue_id_t h_queue,
                                uint32_t *        p_previous_drop_count);

/** @brief Run the regression scenarios and report success. */
int main (void)
{
    msgbus_queue_id_t h_queue = {0};

    msgbus_queue_config_t config = {.type      = 1U,
                                    .item_size = sizeof(test_message_t),
                                    .depth     = 2U,

                                    /* When the queue is full, discard the
                                       oldest item and keep the newest data. */
                                    .overflow_policy =
                                        UTHOME_MSGBUS_DROP_OLDEST};

    uint32_t previous_drop_count = 0U;
    uint32_t health_count        = 0U;

    msgbus_stats_t stats = {0U};

    /* Initialise both subsystems. */
    assert(UTHOME_MSGBUS_OK == msgbus_init());

    assert(UTHOME_HEALTH_OK == health_init());

    /* Create a queue with only two slots. */
    assert(UTHOME_MSGBUS_OK == msgbus_create_queue(&config, &h_queue));

    test_message_t message1 = {.value = 10};

    test_message_t message2 = {.value = 20};

    test_message_t message3 = {.value = 30};

    /* Fill the queue. Queue: [10] [20] */
    assert(UTHOME_MSGBUS_OK ==
           msgbus_publish(h_queue, 1, &message1, sizeof(message1)));

    assert(UTHOME_MSGBUS_OK ==
           msgbus_publish(h_queue, 1, &message2, sizeof(message2)));

    /* Queue is full. Publishing 30 causes: [10] [20]   drop 10   [20] [30] The
     * publish itself succeeds because DROP_OLDEST makes room for the new item.
     */
    assert(UTHOME_MSGBUS_OK ==
           msgbus_publish(h_queue, 1, &message3, sizeof(message3)));

    /* Verify that one item was dropped. */
    assert(UTHOME_MSGBUS_OK == msgbus_get_stats(h_queue, &stats));

    assert(1 == stats.dropped_count);
    assert(2 == stats.current_depth);

    /* Health checks the queue statistics. Because dropped_count changed from 0
     * to 1, health should record one QUEUE_OVERFLOW event. */
    assert(UTHOME_HEALTH_OK ==
           hlthbus_check_queue(h_queue, 1000, &previous_drop_count));

    assert(1 == previous_drop_count);

    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_QUEUE_OVERFLOW, &health_count));

    assert(1 == health_count);

    /* Checking again without another queue overflow must NOT generate another
     * health event. */
    assert(UTHOME_HEALTH_OK ==
           hlthbus_check_queue(h_queue, 1100, &previous_drop_count));

    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_QUEUE_OVERFLOW, &health_count));

    assert(1 == health_count);

    verify_second_drop(h_queue, &previous_drop_count);

    printf("Health and drop-oldest message bus integration tests passed!\n");

    return 0;
}

/** @brief Verify second drop. */
static void verify_second_drop (msgbus_queue_id_t h_queue,
                                uint32_t *        p_previous_drop_count)
{
    test_message_t message4     = {.value = 40};
    msgbus_stats_t stats        = {0U};
    uint32_t       health_count = 0U;
    /* Queue is still full: [20] [30] Publish 40: drop 20 resulting queue: [30]
     * [40] */
    assert(UTHOME_MSGBUS_OK ==
           msgbus_publish(h_queue, 1, &message4, sizeof(message4)));

    /* There should now be two dropped items. */
    assert(UTHOME_MSGBUS_OK == msgbus_get_stats(h_queue, &stats));

    assert(2 == stats.dropped_count);
    assert(2 == stats.current_depth);

    /* Health detects the second overflow. However, this occurs only 200 ms
     * after the first emitted overflow event. Therefore: - the health event is
     * still counted - output is rate-limited */
    assert(UTHOME_HEALTH_RATE_LIMITED ==
           hlthbus_check_queue(h_queue, 1200, &(*p_previous_drop_count)));

    assert(2 == (*p_previous_drop_count));

    assert(UTHOME_HEALTH_OK ==
           health_get_event_count(UTHOME_HEALTH_QUEUE_OVERFLOW, &health_count));

    assert(2 == health_count);

    /* Final statistics check. */
    assert(UTHOME_MSGBUS_OK == msgbus_get_stats(h_queue, &stats));

    assert(2 == stats.dropped_count);
    assert(2 == stats.high_water_mark);
}

/* End of file. */

