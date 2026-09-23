#ifndef UTHOME_MSGBUS_H
#define UTHOME_MSGBUS_H

#include <stddef.h>
#include <stdint.h>

#define UTHOME_MSGBUS_MAX_QUEUES     4
#define UTHOME_MSGBUS_MAX_DEPTH      8
#define UTHOME_MSGBUS_MAX_ITEM_SIZE  64

typedef int32_t uthome_msgbus_queue_id_t;
typedef uint16_t uthome_msgbus_type_t;

typedef enum {
    UTHOME_MSGBUS_OK = 0,
    UTHOME_MSGBUS_ERR_INVALID = -1,
    UTHOME_MSGBUS_ERR_FULL = -2,
    UTHOME_MSGBUS_ERR_EMPTY = -3,
    UTHOME_MSGBUS_ERR_TYPE = -4,
    UTHOME_MSGBUS_ERR_SIZE = -5,
    UTHOME_MSGBUS_ERR_NO_RESOURCE = -6
} uthome_msgbus_result_t;

typedef struct {
    uthome_msgbus_type_t type;
    size_t item_size;
    size_t depth;
} uthome_msgbus_queue_config_t;

typedef struct {
    uint32_t published_count;
    uint32_t consumed_count;
    uint32_t dropped_count;

    size_t current_depth;
    size_t high_water_mark;
} uthome_msgbus_stats_t;

int uthome_msgbus_init(void);

int uthome_msgbus_create_queue(
    const uthome_msgbus_queue_config_t *config,
    uthome_msgbus_queue_id_t *queue_id
);

int uthome_msgbus_publish(
    uthome_msgbus_queue_id_t queue_id,
    uthome_msgbus_type_t type,
    const void *item,
    size_t item_size
);

int uthome_msgbus_consume(
    uthome_msgbus_queue_id_t queue_id,
    uthome_msgbus_type_t expected_type,
    void *item,
    size_t item_capacity
);

int uthome_msgbus_consume_latest(
    uthome_msgbus_queue_id_t queue_id,
    uthome_msgbus_type_t expected_type,
    void *item,
    size_t item_capacity
);

int uthome_msgbus_get_stats(
    uthome_msgbus_queue_id_t queue_id,
    uthome_msgbus_stats_t *stats
);

#endif