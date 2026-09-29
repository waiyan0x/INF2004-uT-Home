/**
 * @file msgbus.c
 * @brief Provide bounded typed queues and explicit overflow policies.
 */
#include "uthome/msgbus.h"

#include <stdbool.h>
#include <string.h>

typedef struct
{
    bool b_in_use;

    msgbus_type_t type;

    size_t item_size;
    size_t depth;

    msgbus_overflow_policy_t overflow_policy;

    size_t head;
    size_t tail;
    size_t count;

    uint8_t storage[UTHOME_MSGBUS_MAX_DEPTH][UTHOME_MSGBUS_MAX_ITEM_SIZE];

    msgbus_stats_t stats;

} msgbus_queue_t;

static msgbus_queue_t g_queues[UTHOME_MSGBUS_MAX_QUEUES] = {0};

/*
 * Validate queue ID and return
 * the corresponding queue.
 */
static msgbus_queue_t * get_queue (msgbus_queue_id_t h_queue);

/**
 * @brief Clear all bounded queue state and statistics.
 * @return Message-bus success or the specific validation/resource error.
 */
int msgbus_init (void)
{
    memset(g_queues, 0, sizeof(g_queues));

    return UTHOME_MSGBUS_OK;
}

/**
 * @brief Allocate and configure one bounded typed queue.
 * @param p_config Configuration bytes or queue settings to validate.
 * @param ph_queue Destination handle written only after successful allocation.
 * @return Message-bus success or the specific validation/resource error.
 */
int msgbus_create_queue (const msgbus_queue_config_t * p_config,
                         msgbus_queue_id_t *           ph_queue)
{
    if ((NULL == p_config) || (NULL == ph_queue))
    {
        return UTHOME_MSGBUS_ERR_INVALID;
    }

    if ((0U == p_config->item_size) ||
        (p_config->item_size > UTHOME_MSGBUS_MAX_ITEM_SIZE))
    {
        return UTHOME_MSGBUS_ERR_SIZE;
    }

    if ((0U == p_config->depth) || (p_config->depth > UTHOME_MSGBUS_MAX_DEPTH))
    {
        return UTHOME_MSGBUS_ERR_INVALID;
    }

    if ((UTHOME_MSGBUS_DROP_OLDEST != p_config->overflow_policy) &&
        (UTHOME_MSGBUS_REJECT_NEWEST != p_config->overflow_policy))
    {
        return UTHOME_MSGBUS_ERR_INVALID;
    }

    for (size_t index = 0U; index < UTHOME_MSGBUS_MAX_QUEUES; ++index)
    {
        if (!g_queues[index].b_in_use)
        {
            msgbus_queue_t * p_queue = &g_queues[index];

            memset(p_queue, 0, sizeof(*p_queue));

            p_queue->b_in_use = true;

            p_queue->type = p_config->type;

            p_queue->item_size = p_config->item_size;

            p_queue->depth = p_config->depth;

            p_queue->overflow_policy = p_config->overflow_policy;

            /* The fixed queue table is smaller than the signed handle range. */
            *ph_queue = (msgbus_queue_id_t)index;

            return UTHOME_MSGBUS_OK;
        }
    }

    return UTHOME_MSGBUS_ERR_NO_RESOURCE;
}

/**
 * @brief Append a typed item according to the configured overflow policy.
 * @param h_queue Handle of the queue to inspect or operate on.
 * @param type Type identifier of the published item.
 * @param p_item Item buffer containing input bytes or receiving copied output
 * bytes.
 * @param item_size Size of the published item in bytes.
 * @return Message-bus success or the specific validation/resource error.
 */
int msgbus_publish (msgbus_queue_id_t h_queue,
                    msgbus_type_t     type,
                    const void *      p_item,
                    size_t            item_size)
{
    msgbus_queue_t * p_queue = get_queue(h_queue);

    if ((NULL == p_queue) || (NULL == p_item))
    {
        return UTHOME_MSGBUS_ERR_INVALID;
    }

    if (type != p_queue->type)
    {
        return UTHOME_MSGBUS_ERR_TYPE;
    }

    if (item_size != p_queue->item_size)
    {
        return UTHOME_MSGBUS_ERR_SIZE;
    }

    /*
     * Queue is already full.
     */
    if (p_queue->count >= p_queue->depth)
    {
        ++p_queue->stats.dropped_count;

        /*
         * Policy 1:
         *
         * Reject the newly published item.
         */
        if (UTHOME_MSGBUS_REJECT_NEWEST == p_queue->overflow_policy)
        {
            return UTHOME_MSGBUS_ERR_FULL;
        }

        /*
         * Policy 2:
         *
         * Drop the oldest queued item.
         *
         * Move head forward one position,
         * creating room for the new item.
         */
        p_queue->head = (p_queue->head + 1U) % p_queue->depth;

        --p_queue->count;
    }

    /*
     * Store the new item at the tail.
     */
    memcpy(p_queue->storage[p_queue->tail], p_item, p_queue->item_size);

    p_queue->tail = (p_queue->tail + 1U) % p_queue->depth;

    ++p_queue->count;
    ++p_queue->stats.published_count;

    p_queue->stats.current_depth = p_queue->count;

    if (p_queue->count > p_queue->stats.high_water_mark)
    {
        p_queue->stats.high_water_mark = p_queue->count;
    }

    return UTHOME_MSGBUS_OK;
}

/**
 * @brief Copy and remove the oldest queued item.
 * @param h_queue Handle of the queue to inspect or operate on.
 * @param expected_type Type identifier required by the consumer.
 * @param p_item Item buffer containing input bytes or receiving copied output
 * bytes.
 * @param item_capacity Available destination bytes for the copied item.
 * @return Message-bus success or the specific validation/resource error.
 */
int msgbus_consume (msgbus_queue_id_t h_queue,
                    msgbus_type_t     expected_type,
                    void *            p_item,
                    size_t            item_capacity)
{
    msgbus_queue_t * p_queue = get_queue(h_queue);

    if ((NULL == p_queue) || (NULL == p_item))
    {
        return UTHOME_MSGBUS_ERR_INVALID;
    }

    if (expected_type != p_queue->type)
    {
        return UTHOME_MSGBUS_ERR_TYPE;
    }

    if (item_capacity < p_queue->item_size)
    {
        return UTHOME_MSGBUS_ERR_SIZE;
    }

    if (0U == p_queue->count)
    {
        return UTHOME_MSGBUS_ERR_EMPTY;
    }

    memcpy(p_item, p_queue->storage[p_queue->head], p_queue->item_size);

    p_queue->head = (p_queue->head + 1U) % p_queue->depth;

    --p_queue->count;
    ++p_queue->stats.consumed_count;

    p_queue->stats.current_depth = p_queue->count;

    return UTHOME_MSGBUS_OK;
}

/**
 * @brief Copy the newest item and discard older queued samples.
 * @param h_queue Handle of the queue to inspect or operate on.
 * @param expected_type Type identifier required by the consumer.
 * @param p_item Item buffer containing input bytes or receiving copied output
 * bytes.
 * @param item_capacity Available destination bytes for the copied item.
 * @return Message-bus success or the specific validation/resource error.
 */
int msgbus_consume_latest (msgbus_queue_id_t h_queue,
                           msgbus_type_t     expected_type,
                           void *            p_item,
                           size_t            item_capacity)
{
    msgbus_queue_t * p_queue = get_queue(h_queue);

    if ((NULL == p_queue) || (NULL == p_item))
    {
        return UTHOME_MSGBUS_ERR_INVALID;
    }

    if (expected_type != p_queue->type)
    {
        return UTHOME_MSGBUS_ERR_TYPE;
    }

    if (item_capacity < p_queue->item_size)
    {
        return UTHOME_MSGBUS_ERR_SIZE;
    }

    if (0U == p_queue->count)
    {
        return UTHOME_MSGBUS_ERR_EMPTY;
    }

    /*
     * tail points to the next free slot.
     *
     * Therefore the newest item is the
     * position immediately before tail.
     */
    const size_t newest_index =
        (p_queue->tail + p_queue->depth - 1U) % p_queue->depth;

    memcpy(p_item, p_queue->storage[newest_index], p_queue->item_size);

    /*
     * The consumer requested only the
     * newest item.
     *
     * Older queued samples are stale,
     * therefore clear the queue.
     */
    p_queue->head = p_queue->tail;

    p_queue->count = 0U;

    ++p_queue->stats.consumed_count;

    p_queue->stats.current_depth = 0U;

    return UTHOME_MSGBUS_OK;
}

/**
 * @brief Copy queue counters and occupancy to the caller.
 * @param h_queue Handle of the queue to inspect or operate on.
 * @param p_stats Destination for a snapshot of the queue statistics.
 * @return Message-bus success or the specific validation/resource error.
 */
int msgbus_get_stats (msgbus_queue_id_t h_queue, msgbus_stats_t * p_stats)
{
    const msgbus_queue_t * p_queue = get_queue(h_queue);

    if ((NULL == p_queue) || (NULL == p_stats))
    {
        return UTHOME_MSGBUS_ERR_INVALID;
    }

    *p_stats = p_queue->stats;

    return UTHOME_MSGBUS_OK;
}

/**
 * @brief Validate a queue handle and locate its allocated queue.
 * @param h_queue Handle of the queue to inspect or operate on.
 * @return Pointer to the allocated queue, or NULL for an invalid handle.
 */
static msgbus_queue_t * get_queue (msgbus_queue_id_t h_queue)
{
    /* Short-circuit evaluation proves the handle nonnegative before the cast.
     */
    if ((h_queue < 0) || ((size_t)h_queue >= UTHOME_MSGBUS_MAX_QUEUES))
    {
        return NULL;
    }

    msgbus_queue_t * p_queue = &g_queues[h_queue];

    if (!p_queue->b_in_use)
    {
        return NULL;
    }

    return p_queue;
}

/* End of file. */

