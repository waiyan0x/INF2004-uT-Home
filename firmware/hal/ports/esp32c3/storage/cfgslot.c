/**
 * @file cfgslot.c
 * @brief Verify fixed flash layout and isolate two independently erased slots.
 */
#include "cfgslot.h"

#include <string.h>
#include "cfgpart.h"

static bool valid_operations (const cfgslot_ops_t * p_operations);
static bool valid_range (const cfgslot_t * p_store,
                         unsigned          slot,
                         size_t            offset,
                         size_t            size);

/** @brief Verify size and all table chunks before enabling storage. */
int cfgslot_open (cfgslot_t * p_store)
{
    if ((NULL == p_store) || (!valid_operations(p_store->p_operations)) ||
        (!p_store->p_operations->p_owner(p_store->p_context)) ||
        (p_store->b_ready))
    {
        return -1;
    }
    uint32_t flash_size = 0U;
    if ((0 != p_store->p_operations->p_size(p_store->p_context, &flash_size)) ||
        (UTHOME_PART_FLASH_BYTES != flash_size))
    {
        return -1;
    }
    for (size_t offset = 0U; offset < UTHOME_PART_TABLE_BYTES;
         offset += UTHOME_CFGSLOT_CHUNK)
    {
        /* The complete table is an exact multiple of the internal-RAM chunk. */
        if ((0 !=
             p_store->p_operations->p_table(p_store->p_context,
                                            UTHOME_PART_TABLE_OFFSET + offset,
                                            p_store->scratch,
                                            UTHOME_CFGSLOT_CHUNK)) ||
            (!cfgpart_check(p_store->scratch, offset, UTHOME_CFGSLOT_CHUNK)))
        {
            return -1;
        }
    }
    p_store->b_ready = true;
    return 0;
}

/** @brief Read through fixed internal-RAM scratch, copying complete chunks. */
int cfgslot_read (cfgslot_t * p_store,
                  unsigned    slot,
                  size_t      offset,
                  uint8_t *   p_data,
                  size_t      size)
{
    if ((NULL == p_data) || (!valid_range(p_store, slot, offset, size)))
    {
        return -1;
    }
    for (size_t done = 0U; done < size;)
    {
        const size_t remaining = size - done;
        const size_t chunk     = (remaining < UTHOME_CFGSLOT_CHUNK)
                                     ? remaining
                                     : UTHOME_CFGSLOT_CHUNK;
        if (0 != p_store->p_operations->p_read(p_store->p_context,
                                               (slot * UTHOME_CFGSLOT_BYTES) +
                                                   offset + done,
                                               p_store->scratch,
                                               chunk))
        {
            return -1;
        }
        memcpy(&p_data[done], p_store->scratch, chunk);
        done += chunk;
    }
    return 0;
}

/** @brief Program exactly the requested slot-relative byte range. */
int cfgslot_write (cfgslot_t *     p_store,
                   unsigned        slot,
                   size_t          offset,
                   const uint8_t * p_data,
                   size_t          size)
{
    if ((NULL == p_data) || (!valid_range(p_store, slot, offset, size)))
    {
        return -1;
    }
    return (0 == p_store->p_operations->p_write(p_store->p_context,
                                                (slot * UTHOME_CFGSLOT_BYTES) +
                                                    offset,
                                                p_data,
                                                size))
               ? 0
               : -1;
}

/** @brief Erase precisely one three-sector slot, preserving its peer. */
int cfgslot_erase (cfgslot_t * p_store, unsigned slot)
{
    if (!valid_range(p_store, slot, 0U, UTHOME_CFGSLOT_BYTES))
    {
        return -1;
    }
    return (0 == p_store->p_operations->p_erase(p_store->p_context,
                                                slot * UTHOME_CFGSLOT_BYTES,
                                                UTHOME_CFGSLOT_BYTES))
               ? 0
               : -1;
}

/** @brief Synchronous callbacks leave no pending buffer to flush. */
int cfgslot_sync (const cfgslot_t * p_store)
{
    return valid_range(p_store, 0U, 0U, 0U) ? 0 : -1;
}

/** @brief Deny every write until layout verification has completed. */
bool cfgslot_region_allowed (const cfgslot_t * p_store,
                             size_t            address,
                             size_t            size)
{
    return (NULL != p_store) && (p_store->b_ready) &&
           (cfgpart_writable(address, size));
}

/** @brief Require all operations before asking the backend about its flash. */
static bool valid_operations (const cfgslot_ops_t * p_operations)
{
    return (NULL != p_operations) && (NULL != p_operations->p_owner) &&
           (NULL != p_operations->p_size) && (NULL != p_operations->p_table) &&
           (NULL != p_operations->p_read) && (NULL != p_operations->p_write) &&
           (NULL != p_operations->p_erase);
}

/** @brief Bound subtraction before any slot/offset addition can wrap. */
static bool valid_range (const cfgslot_t * p_store,
                         unsigned          slot,
                         size_t            offset,
                         size_t            size)
{
    return (NULL != p_store) && (p_store->b_ready) &&
           (p_store->p_operations->p_owner(p_store->p_context)) &&
           (slot < 2U) && (offset <= UTHOME_CFGSLOT_BYTES) &&
           (size <= (UTHOME_CFGSLOT_BYTES - offset));
}

/* End of file. */

