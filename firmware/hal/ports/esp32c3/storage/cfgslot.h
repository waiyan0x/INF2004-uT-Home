/**
 * @file cfgslot.h
 * @brief Checked fixed-layout slot access through synchronous flash callbacks.
 */
#ifndef UTHOME_CFGSLOT_H
#define UTHOME_CFGSLOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define UTHOME_CFGSLOT_BYTES 12288U
#define UTHOME_CFGSLOT_CHUNK 256U

typedef bool cfgslot_owner_t (void * p_context);
typedef int cfgslot_size_t (void * p_context, uint32_t * p_size);
typedef int
cfgslot_read_t (void * p_context, size_t offset, uint8_t * p_data, size_t size);
typedef int cfgslot_write_t (void *          p_context,
                             size_t          offset,
                             const uint8_t * p_data,
                             size_t          size);
typedef int cfgslot_erase_t (void * p_context, size_t offset, size_t size);

/* All I/O is synchronous, returns zero on success and never reenters.
 * p_table reads physical flash; read/write/erase use partition-relative
 * offsets. p_size rejects a missing/internal-flash mismatch before returning
 * its size.
 */
typedef struct
{
    cfgslot_owner_t * p_owner;
    cfgslot_size_t *  p_size;
    cfgslot_read_t *  p_table;
    cfgslot_read_t *  p_read;
    cfgslot_write_t * p_write;
    cfgslot_erase_t * p_erase;
} cfgslot_ops_t;

/* Initialize callbacks/context and zero the rest before use. On the target,
 * retain this object in internal DRAM for the entire boot, avoiding SDK bounce
 * allocation. Operations/context remain live. Caller buffers may not alias it.
 */
typedef struct
{
    const cfgslot_ops_t * p_operations;
    void *                p_context;
    bool                  b_ready;
    uint8_t               scratch[UTHOME_CFGSLOT_CHUNK];
} cfgslot_t;

/* Verify every canonical table byte before publishing ready. Never erase.
 * Failed verification may be retried by the serialized owner; success may not.
 */
int cfgslot_open (cfgslot_t * p_store);
int cfgslot_read (cfgslot_t * p_store,
                  unsigned    slot,
                  size_t      offset,
                  uint8_t *   p_data,
                  size_t      size);
int cfgslot_write (cfgslot_t *     p_store,
                   unsigned        slot,
                   size_t          offset,
                   const uint8_t * p_data,
                   size_t          size);
int cfgslot_erase (cfgslot_t * p_store, unsigned slot);
int cfgslot_sync (const cfgslot_t * p_store);
/* Flash guard: no callbacks, kernel access, logging or allocation. */
bool cfgslot_region_allowed (const cfgslot_t * p_store,
                             size_t            address,
                             size_t            size);

#endif

/* End of file. */

