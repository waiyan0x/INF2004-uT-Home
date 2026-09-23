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
        .depth = 2
    };

    uint32_t previous_drop_count = 0;
    uint32_t health_count = 0;


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


    test_message_t message = {
        .value = 1
    };


    /*
     * Fill both available slots.
     */
    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &message,
            sizeof(message)
        ) == UTHOME_MSGBUS_OK
    );

    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &message,
            sizeof(message)
        ) == UTHOME_MSGBUS_OK
    );


    /*
     * Third item causes overflow.
     */
    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &message,
            sizeof(message)
        ) == UTHOME_MSGBUS_ERR_FULL
    );


    /*
     * Health monitor checks queue statistics
     * and detects the overflow.
     */
    assert(
        uthome_health_check_queue(
            queue_id,
            1000,
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
     * Checking again without another overflow
     * must not create another health event.
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
     * Cause another overflow.
     */
    assert(
        uthome_msgbus_publish(
            queue_id,
            1,
            &message,
            sizeof(message)
        ) == UTHOME_MSGBUS_ERR_FULL
    );


    /*
     * This overflow happens too soon after the
     * previous emitted health event, therefore
     * health should count it but rate-limit output.
     */
    assert(
        uthome_health_check_queue(
            queue_id,
            1200,
            &previous_drop_count
        ) == UTHOME_HEALTH_RATE_LIMITED
    );


    assert(
        uthome_health_get_event_count(
            UTHOME_HEALTH_QUEUE_OVERFLOW,
            &health_count
        ) == UTHOME_HEALTH_OK
    );

    assert(health_count == 2);


    printf(
        "Health and message bus integration tests passed!\n"
    );

    return 0;
}