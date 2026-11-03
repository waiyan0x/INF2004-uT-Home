/**
 * @file netble.h
 * @brief Versioned sensor frames and bounded BLE notification ownership.
 */
#ifndef UTHOME_NETBLE_H
#define UTHOME_NETBLE_H
#include "sampcache.h"
#define UTHOME_BLE_FRAME           112U
#define UTHOME_BLE_FRAGMENT_HEADER 8U

typedef struct
{
    uint64_t boot_id;
    uint32_t active_revision;
    uint32_t generation;
} netble_meta_t;

typedef struct
{
    uint8_t  active[UTHOME_BLE_FRAME];
    uint8_t  pending[UTHOME_BLE_FRAME];
    uint32_t active_generation;
    uint32_t pending_generation;
    uint32_t replaced;
    uint16_t sequence;
    uint16_t payload_bytes;
    uint8_t  index;
    bool     b_active;
    bool     b_pending;
    bool     b_fragment;
} netble_t;

/* Encode copied cache data, never driver/queue access. All integers explicitly
 * little endian, floats IEEE binary32. Destination >=112 bytes; no aliasing.
 * Only expected sources/types 1..5 are accepted. Missing/fault measurements
 * have zero value bytes and distinct quality. See the network guide offsets.
 */
int netble_encode (sampcache_entry_t const * p_entry,
                   netble_meta_t const *     p_meta,
                   uint64_t                  now_ms,
                   uint8_t *                 p_output,
                   size_t                    capacity);
/* Zero-init/reset on disconnect; one context per connection, one owner.
 * Offers replace pending only, leaving active fragments immutable.
 */
void netble_reset (netble_t * p_ble);
int netble_offer (netble_t *                p_ble,
                  sampcache_entry_t const * p_entry,
                  netble_meta_t const *     p_meta,
                  uint64_t                  now_ms);
/* 0 yields a fragment; 1 empty; -1 invalid/capacity. MTU 23..517. Repeated
 * calls return the same fragment until sent confirms successful SDK enqueue.
 * If SDK returns busy, retry later without calling sent. MTU is frozen for an
 * active frame. Generation mismatch drops old pending/active frames. On a new
 * boot or disconnect reset context before use. Output must not alias context.
 */
int netble_fragment (netble_t * p_ble,
                     uint32_t   generation,
                     uint16_t   mtu,
                     uint8_t *  p_output,
                     size_t     capacity,
                     size_t *   p_size);
int netble_sent (netble_t * p_ble);
#endif

/* End of file. */

