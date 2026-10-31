/**
 * @file netring.h
 * @brief Exact-capacity ring over caller-owned fixed network storage.
 */
#ifndef UTHOME_NETRING_H
#define UTHOME_NETRING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
    NETRING_BACK = 0,
    NETRING_FRONT,
    NETRING_REPLACE
} netring_position_t;

typedef struct
{
    uint8_t * p_storage;
    size_t    storage_bytes;
    uint32_t  capacity;
    uint32_t  item_bytes;
    uint32_t  head;
    uint32_t  count;
} netring_t;

/** @brief Bind storage; failure leaves the previous state unchanged. */
int netring_init (netring_t * p_ring,
                  uint8_t *   p_storage,
                  size_t      storage_bytes,
                  uint32_t    capacity,
                  uint32_t    item_bytes);

/** @brief Check metadata before deriving a storage offset. */
bool netring_valid (netring_t const * p_ring);

/** @brief Empty a valid ring without changing its storage reservation. */
int netring_reset (netring_t * p_ring);

/**
 * @brief Copy one item; replacement is supported only at capacity one.
 *
 * All operations require external serialization, including metadata reads.
 * Item/output buffers must not overlap ring storage or metadata. This module
 * never allocates, waits, signals an object or changes interrupt state.
 */
int netring_send (netring_t *        p_ring,
                  void const *       p_item,
                  netring_position_t position);

/** @brief Copy the oldest item without consuming it. */
int netring_peek (netring_t const * p_ring, void * p_item);

/** @brief Copy and consume the oldest item; null output explicitly discards. */
int netring_receive (netring_t * p_ring, void * p_item);

#endif

/* End of file. */

