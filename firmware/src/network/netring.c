/**
 * @file netring.c
 * @brief Exact-capacity ring over caller-owned fixed network storage.
 */
#include "netring.h"

#include <string.h>

/** @brief Bind storage; failure leaves the previous state unchanged. */
int netring_init (netring_t * p_ring,
                  uint8_t *   p_storage,
                  size_t      storage_bytes,
                  uint32_t    capacity,
                  uint32_t    item_bytes)
{
    netring_t candidate = {0};

    candidate.p_storage     = p_storage;
    candidate.storage_bytes = storage_bytes;
    candidate.capacity      = capacity;
    candidate.item_bytes    = item_bytes;
    if ((NULL == p_ring) || (!netring_valid(&candidate)))
    {
        return -1;
    }
    *p_ring = candidate;
    return 0;
}

/** @brief Validate multiplication and indices without accessing storage. */
bool netring_valid (netring_t const * p_ring)
{
    if ((NULL == p_ring) || (NULL == p_ring->p_storage) ||
        (0U == p_ring->capacity) || (0U == p_ring->item_bytes))
    {
        return false;
    }
    return (
        (p_ring->capacity <= (p_ring->storage_bytes / p_ring->item_bytes)) &&
        (p_ring->head < p_ring->capacity) &&
        (p_ring->count <= p_ring->capacity));
}

/** @brief Empty a valid ring without reallocating or clearing its storage. */
int netring_reset (netring_t * p_ring)
{
    if (!netring_valid(p_ring))
    {
        return -1;
    }
    p_ring->head  = 0U;
    p_ring->count = 0U;
    return 0;
}

/** @brief Preserve the full configured capacity, including length one. */
int netring_send (netring_t *        p_ring,
                  void const *       p_item,
                  netring_position_t position)
{
    uint32_t index     = 0U;
    uint32_t remaining = 0U;

    if ((!netring_valid(p_ring)) || (NULL == p_item) ||
        ((NETRING_BACK != position) && (NETRING_FRONT != position) &&
         (NETRING_REPLACE != position)))
    {
        return -1;
    }
    if (NETRING_REPLACE == position)
    {
        if (1U != p_ring->capacity)
        {
            return -1;
        }
        (void)memcpy(p_ring->p_storage, p_item, p_ring->item_bytes);
        p_ring->count = 1U;
        return 0;
    }
    if (p_ring->count == p_ring->capacity)
    {
        return -1;
    }
    if (NETRING_FRONT == position)
    {
        index = (0U == p_ring->head) ? (p_ring->capacity - 1U)
                                     : (p_ring->head - 1U);
    }
    else
    {
        remaining = p_ring->capacity - p_ring->head;
        index     = (p_ring->count < remaining) ? (p_ring->head + p_ring->count)
                                                : (p_ring->count - remaining);
    }
    (void)memcpy(p_ring->p_storage + ((size_t)index * p_ring->item_bytes),
                 p_item,
                 p_ring->item_bytes);
    if (NETRING_FRONT == position)
    {
        p_ring->head = index;
    }
    ++p_ring->count;
    return 0;
}

/** @brief Copy without consuming; empty/invalid inputs leave output intact. */
int netring_peek (netring_t const * p_ring, void * p_item)
{
    if ((!netring_valid(p_ring)) || (NULL == p_item) || (0U == p_ring->count))
    {
        return -1;
    }
    (void)memcpy(p_item,
                 p_ring->p_storage +
                     ((size_t)p_ring->head * p_ring->item_bytes),
                 p_ring->item_bytes);
    return 0;
}

/** @brief Consume one item only after validating the complete metadata. */
int netring_receive (netring_t * p_ring, void * p_item)
{
    if ((!netring_valid(p_ring)) || (0U == p_ring->count))
    {
        return -1;
    }
    if (NULL != p_item)
    {
        (void)memcpy(p_item,
                     p_ring->p_storage +
                         ((size_t)p_ring->head * p_ring->item_bytes),
                     p_ring->item_bytes);
    }
    ++p_ring->head;
    if (p_ring->head == p_ring->capacity)
    {
        p_ring->head = 0U;
    }
    --p_ring->count;
    return 0;
}

/* End of file. */

