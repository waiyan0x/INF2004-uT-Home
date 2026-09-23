#include "uthome/msgbus.h"

#include <stdbool.h>
#include <string.h>


typedef struct {
    bool in_use;

    uthome_msgbus_type_t type;

    size_t item_size;
    size_t depth;

    size_t head;
    size_t tail;
    size_t count;

    unsigned char storage
        [UTHOME_MSGBUS_MAX_DEPTH]
        [UTHOME_MSGBUS_MAX_ITEM_SIZE];

    uthome_msgbus_stats_t stats;
} msgbus_queue_t;


static msgbus_queue_t
    g_queues[UTHOME_MSGBUS_MAX_QUEUES];


/*
 * Validate a queue ID and return its slot.
 */
static msgbus_queue_t *get_queue(
    uthome_msgbus_queue_id_t queue_id)
{
    if (queue_id < 0 ||
        queue_id >= UTHOME_MSGBUS_MAX_QUEUES) {

        return NULL;
    }

    msgbus_queue_t *queue =
        &g_queues[queue_id];

    if (!queue->in_use) {
        return NULL;
    }

    return queue;
}


int uthome_msgbus_init(void)
{
    memset(
        g_queues,
        0,
        sizeof(g_queues)
    );

    return UTHOME_MSGBUS_OK;
}


int uthome_msgbus_create_queue(
    const uthome_msgbus_queue_config_t *config,
    uthome_msgbus_queue_id_t *queue_id)
{
    if (config == NULL ||
        queue_id == NULL) {

        return UTHOME_MSGBUS_ERR_INVALID;
    }

    if (config->item_size == 0 ||
        config->item_size >
            UTHOME_MSGBUS_MAX_ITEM_SIZE) {

        return UTHOME_MSGBUS_ERR_SIZE;
    }

    if (config->depth == 0 ||
        config->depth >
            UTHOME_MSGBUS_MAX_DEPTH) {

        return UTHOME_MSGBUS_ERR_INVALID;
    }

    for (size_t i = 0;
         i < UTHOME_MSGBUS_MAX_QUEUES;
         ++i) {

        if (!g_queues[i].in_use) {

            msgbus_queue_t *queue =
                &g_queues[i];

            memset(
                queue,
                0,
                sizeof(*queue)
            );

            queue->in_use = true;
            queue->type = config->type;
            queue->item_size =
                config->item_size;
            queue->depth =
                config->depth;

            *queue_id =
                (uthome_msgbus_queue_id_t)i;

            return UTHOME_MSGBUS_OK;
        }
    }

    return UTHOME_MSGBUS_ERR_NO_RESOURCE;
}


int uthome_msgbus_publish(
    uthome_msgbus_queue_id_t queue_id,
    uthome_msgbus_type_t type,
    const void *item,
    size_t item_size)
{
    msgbus_queue_t *queue =
        get_queue(queue_id);

    if (queue == NULL ||
        item == NULL) {

        return UTHOME_MSGBUS_ERR_INVALID;
    }

    if (type != queue->type) {
        return UTHOME_MSGBUS_ERR_TYPE;
    }

    if (item_size != queue->item_size) {
        return UTHOME_MSGBUS_ERR_SIZE;
    }

    /*
     * Queue is full.
     *
     * Do not block indefinitely.
     * Reject the new item and count it.
     */
    if (queue->count >= queue->depth) {

        ++queue->stats.dropped_count;

        return UTHOME_MSGBUS_ERR_FULL;
    }

    memcpy(
        queue->storage[queue->tail],
        item,
        queue->item_size
    );

    queue->tail =
        (queue->tail + 1) %
        queue->depth;

    ++queue->count;
    ++queue->stats.published_count;

    queue->stats.current_depth =
        queue->count;

    if (queue->count >
        queue->stats.high_water_mark) {

        queue->stats.high_water_mark =
            queue->count;
    }

    return UTHOME_MSGBUS_OK;
}


int uthome_msgbus_consume(
    uthome_msgbus_queue_id_t queue_id,
    uthome_msgbus_type_t expected_type,
    void *item,
    size_t item_capacity)
{
    msgbus_queue_t *queue =
        get_queue(queue_id);

    if (queue == NULL ||
        item == NULL) {

        return UTHOME_MSGBUS_ERR_INVALID;
    }

    if (expected_type != queue->type) {
        return UTHOME_MSGBUS_ERR_TYPE;
    }

    if (item_capacity < queue->item_size) {
        return UTHOME_MSGBUS_ERR_SIZE;
    }

    if (queue->count == 0) {
        return UTHOME_MSGBUS_ERR_EMPTY;
    }

    memcpy(
        item,
        queue->storage[queue->head],
        queue->item_size
    );

    queue->head =
        (queue->head + 1) %
        queue->depth;

    --queue->count;
    ++queue->stats.consumed_count;

    queue->stats.current_depth =
        queue->count;

    return UTHOME_MSGBUS_OK;
}


int uthome_msgbus_consume_latest(
    uthome_msgbus_queue_id_t queue_id,
    uthome_msgbus_type_t expected_type,
    void *item,
    size_t item_capacity)
{
    msgbus_queue_t *queue =
        get_queue(queue_id);

    if (queue == NULL ||
        item == NULL) {

        return UTHOME_MSGBUS_ERR_INVALID;
    }

    if (expected_type != queue->type) {
        return UTHOME_MSGBUS_ERR_TYPE;
    }

    if (item_capacity < queue->item_size) {
        return UTHOME_MSGBUS_ERR_SIZE;
    }

    if (queue->count == 0) {
        return UTHOME_MSGBUS_ERR_EMPTY;
    }

    /*
     * tail points to the next free slot,
     * therefore the newest item is one
     * position before tail.
     */
    size_t newest_index =
        (queue->tail +
         queue->depth - 1) %
        queue->depth;

    memcpy(
        item,
        queue->storage[newest_index],
        queue->item_size
    );

    /*
     * Consuming latest clears all queued
     * stale items.
     */
    queue->head = queue->tail;
    queue->count = 0;

    ++queue->stats.consumed_count;

    queue->stats.current_depth = 0;

    return UTHOME_MSGBUS_OK;
}


int uthome_msgbus_get_stats(
    uthome_msgbus_queue_id_t queue_id,
    uthome_msgbus_stats_t *stats)
{
    msgbus_queue_t *queue =
        get_queue(queue_id);

    if (queue == NULL ||
        stats == NULL) {

        return UTHOME_MSGBUS_ERR_INVALID;
    }

    *stats = queue->stats;

    return UTHOME_MSGBUS_OK;
}