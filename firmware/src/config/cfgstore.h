/**
 * @file cfgstore.h
 * @brief Keep two independently erasable configuration records.
 */
#ifndef UTHOME_CFGSTORE_H
#define UTHOME_CFGSTORE_H
#include <stdbool.h>
#include "cfgbytes.h"
#define UTHOME_CFGSTORE_HEADER   32U
#define UTHOME_CFGSTORE_MIN_SLOT (UTHOME_CONFIG_BYTES + UTHOME_CFGSTORE_HEADER)
typedef int cfgstore_read_t (void *    p_context,
                             unsigned  slot,
                             size_t    offset,
                             uint8_t * p_data,
                             size_t    size);
typedef int cfgstore_write_t (void *          p_context,
                              unsigned        slot,
                              size_t          offset,
                              const uint8_t * p_data,
                              size_t          size);
typedef int cfgstore_erase_t (void * p_context, unsigned slot);
typedef int cfgstore_sync_t (void * p_context);

typedef struct
{
    cfgstore_read_t *  p_read;
    cfgstore_write_t * p_write;
    cfgstore_erase_t * p_erase;
    cfgstore_sync_t *  p_sync;
} cfgstore_ops_t;

typedef struct
{
    uint32_t revision;
    uint32_t crc;
    size_t   size;
    unsigned slot;
} cfgstore_record_t;

typedef struct
{
    const cfgstore_ops_t * p_operations;
    void *                 p_context;
    size_t                 slot_bytes;
    uint8_t                scratch[UTHOME_CONFIG_BYTES];
    bool                   b_busy;
} cfgstore_t;

typedef enum
{
    CFGSTORE_OK       = 0,
    CFGSTORE_EMPTY    = 1,
    CFGSTORE_INVALID  = -1,
    CFGSTORE_IO       = -2,
    CFGSTORE_CONFLICT = -3
} cfgstore_result_t;

/* Single serialized owner, callbacks do not reenter. Backend reads exactly the
 * requested bytes, erases to 0xFF, supports byte-range writes in a slot, and
 * syncs durability. Erase/write must never disturb the other slot. No backend
 * is implicitly selected. Slot capacity >=MIN_SLOT, rounded for actual flash.
 * read/save work buffers may not alias scratch. A failed save can already be
 * durable; reread to determine saved state. Revision zero and wrap are invalid.
 * Loading checks record integrity only; callers must independently admit JSON.
 */
int cfgstore_load (cfgstore_t *        p_store,
                   uint8_t *           p_data,
                   size_t              capacity,
                   cfgstore_record_t * p_record);
int cfgstore_save (cfgstore_t *    p_store,
                   const uint8_t * p_data,
                   size_t          size,
                   uint32_t        revision);
#endif
/* End of file. */

