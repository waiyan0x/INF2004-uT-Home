/**
 * @file msgbus.h
 * @brief Exchange copied typed samples through bounded message queues.
 */
#ifndef UTHOME_MSGBUS_H
#define UTHOME_MSGBUS_H

#include <stddef.h>
#include <stdint.h>

#define UTHOME_MSGBUS_MAX_QUEUES    4U
#define UTHOME_MSGBUS_MAX_DEPTH     8U
#define UTHOME_MSGBUS_MAX_ITEM_SIZE 64U

typedef int32_t  msgbus_queue_id_t;
typedef uint16_t msgbus_type_t;

/*
 * Queue overflow behaviour.
 *
 * DROP_OLDEST is the first-prototype
 * telemetry policy so the latest data
 * remains available.
 */
typedef enum
{
    UTHOME_MSGBUS_DROP_OLDEST = 0,
    UTHOME_MSGBUS_REJECT_NEWEST

} msgbus_overflow_policy_t;

typedef enum
{
    UTHOME_MSGBUS_OK = 0,

    UTHOME_MSGBUS_ERR_INVALID     = -1,
    UTHOME_MSGBUS_ERR_FULL        = -2,
    UTHOME_MSGBUS_ERR_EMPTY       = -3,
    UTHOME_MSGBUS_ERR_TYPE        = -4,
    UTHOME_MSGBUS_ERR_SIZE        = -5,
    UTHOME_MSGBUS_ERR_NO_RESOURCE = -6

} msgbus_result_t;

typedef struct
{
    msgbus_type_t type;

    size_t item_size;
    size_t depth;

    msgbus_overflow_policy_t overflow_policy;

} msgbus_queue_config_t;

typedef struct
{
    uint32_t published_count;
    uint32_t consumed_count;
    uint32_t dropped_count;

    size_t current_depth;
    size_t high_water_mark;

} msgbus_stats_t;

int msgbus_init (void);

int msgbus_create_queue (const msgbus_queue_config_t * p_config,
                         msgbus_queue_id_t *           ph_queue_id);

int msgbus_publish (msgbus_queue_id_t h_queue_id,
                    msgbus_type_t     type,
                    const void *      p_item,
                    size_t            item_size);

int msgbus_consume (msgbus_queue_id_t h_queue_id,
                    msgbus_type_t     expected_type,
                    void *            p_item,
                    size_t            item_capacity);

int msgbus_consume_latest (msgbus_queue_id_t h_queue_id,
                           msgbus_type_t     expected_type,
                           void *            p_item,
                           size_t            item_capacity);

int msgbus_get_stats (msgbus_queue_id_t h_queue_id, msgbus_stats_t * p_stats);

#endif

/* End of file. */

