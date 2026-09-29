/**
 * @file cfgstore.c
 * @brief Commit verified data in the inactive slot, leaving the old record
 * intact.
 */
#include "cfgstore.h"
#include <string.h>
static bool valid_store (const cfgstore_t * p_store);
static int inspect_slot (cfgstore_t *        p_store,
                         unsigned            slot,
                         cfgstore_record_t * p_record);
static int select_record (cfgstore_t * p_store, cfgstore_record_t * p_record);
static int load_record (cfgstore_t *        p_store,
                        uint8_t *           p_data,
                        size_t              capacity,
                        cfgstore_record_t * p_record);
static int write_record (cfgstore_t *    p_store,
                         const uint8_t * p_data,
                         size_t          size,
                         uint32_t        revision);
static int commit_record (cfgstore_t *    p_store,
                          unsigned        slot,
                          const uint8_t * p_data,
                          size_t          size,
                          uint32_t        revision);
static const uint32_t g_magic = UINT32_C(0x43485455);

static const uint32_t g_commit = UINT32_C(0xC35A3CC3);

/** @brief Fail closed on unreadable slots instead of guessing which is newer.
 */
int cfgstore_load (cfgstore_t *        p_store,
                   uint8_t *           p_data,
                   size_t              capacity,
                   cfgstore_record_t * p_record)
{
    if ((!valid_store(p_store)) || (NULL == p_data) || (NULL == p_record) ||
        (p_store->b_busy))
    {
        return CFGSTORE_INVALID;
    }
    p_store->b_busy  = true;
    *p_record        = (cfgstore_record_t){0};
    const int result = (0 == p_store->p_operations->p_sync(p_store->p_context))
                           ? load_record(p_store, p_data, capacity, p_record)
                           : CFGSTORE_IO;
    p_store->b_busy  = false;
    return result;
}

/** @brief Save only a nonempty admitted payload after successful activation. */
int cfgstore_save (cfgstore_t *    p_store,
                   const uint8_t * p_data,
                   size_t          size,
                   uint32_t        revision)
{
    if ((!valid_store(p_store)) || (NULL == p_data) || (0U == size) ||
        (size > UTHOME_CONFIG_BYTES) || (0U == revision) || (p_store->b_busy))
    {
        return CFGSTORE_INVALID;
    }
    p_store->b_busy  = true;
    const int result = (0 == p_store->p_operations->p_sync(p_store->p_context))
                           ? write_record(p_store, p_data, size, revision)
                           : CFGSTORE_IO;
    p_store->b_busy  = false;
    return result;
}

/** @brief All operations are mandatory, including durability synchronization.
 */
static bool valid_store (const cfgstore_t * p_store)
{
    return (NULL != p_store) && (NULL != p_store->p_operations) &&
           (p_store->slot_bytes >= UTHOME_CFGSTORE_MIN_SLOT) &&
           (NULL != p_store->p_operations->p_read) &&
           (NULL != p_store->p_operations->p_write) &&
           (NULL != p_store->p_operations->p_erase) &&
           (NULL != p_store->p_operations->p_sync);
}

/** @brief Invalid or interrupted records are ignored, never partially loaded.
 */
static int
inspect_slot (cfgstore_t * p_store, unsigned slot, cfgstore_record_t * p_record)
{
    uint8_t header[UTHOME_CFGSTORE_HEADER] = {0};
    if (0 != p_store->p_operations->p_read(
                 p_store->p_context, slot, 0U, header, sizeof(header)))
    {
        return CFGSTORE_IO;
    }
    const uint32_t size = cfgbytes_get(&header[12]);
    if ((g_magic != cfgbytes_get(header)) || (1U != cfgbytes_get(&header[4])) ||
        (0U == cfgbytes_get(&header[8])) || (0U == size) ||
        (size > UTHOME_CONFIG_BYTES) ||
        (g_commit != cfgbytes_get(&header[24])) ||
        (UINT32_MAX != cfgbytes_get(&header[28])) ||
        ((~cfgbytes_crc(UINT32_MAX, header, 20U)) != cfgbytes_get(&header[20])))
    {
        return CFGSTORE_EMPTY;
    }
    if (0 != p_store->p_operations->p_read(p_store->p_context,
                                           slot,
                                           UTHOME_CFGSTORE_HEADER,
                                           p_store->scratch,
                                           size))
    {
        return CFGSTORE_IO;
    }
    const uint32_t crc = ~cfgbytes_crc(UINT32_MAX, p_store->scratch, size);
    if (crc != cfgbytes_get(&header[16]))
    {
        return CFGSTORE_EMPTY;
    }
    *p_record = (cfgstore_record_t){.revision = cfgbytes_get(&header[8]),
                                    .crc      = crc,
                                    .size     = size,
                                    .slot     = slot};
    return CFGSTORE_OK;
}

/** @brief Revisions increase without wrapping; equal committed revisions
 * conflict. */
static int select_record (cfgstore_t * p_store, cfgstore_record_t * p_record)
{
    cfgstore_record_t records[2] = {0};
    const int         first      = inspect_slot(p_store, 0U, &records[0]);
    const int         second     = inspect_slot(p_store, 1U, &records[1]);
    if ((CFGSTORE_IO == first) || (CFGSTORE_IO == second))
    {
        return CFGSTORE_IO;
    }
    if ((CFGSTORE_OK == first) && (CFGSTORE_OK == second) &&
        (records[0].revision == records[1].revision))
    {
        return CFGSTORE_CONFLICT;
    }
    if ((CFGSTORE_EMPTY == first) && (CFGSTORE_EMPTY == second))
    {
        return CFGSTORE_EMPTY;
    }
    *p_record = ((CFGSTORE_OK == second) &&
                 ((CFGSTORE_EMPTY == first) ||
                  (records[1].revision > records[0].revision)))
                    ? records[1]
                    : records[0];
    return CFGSTORE_OK;
}

/** @brief Recheck the chosen record before exposing bytes to the application.
 */
static int load_record (cfgstore_t *        p_store,
                        uint8_t *           p_data,
                        size_t              capacity,
                        cfgstore_record_t * p_record)
{
    cfgstore_record_t selected = {0};
    int               result   = select_record(p_store, &selected);
    if (CFGSTORE_OK != result)
    {
        return result;
    }
    if (capacity < selected.size)
    {
        return CFGSTORE_INVALID;
    }
    cfgstore_record_t verified = {0};
    result = inspect_slot(p_store, selected.slot, &verified);
    if ((CFGSTORE_OK != result) || (verified.revision != selected.revision) ||
        (verified.crc != selected.crc) || (verified.size != selected.size))
    {
        return CFGSTORE_IO;
    }
    memcpy(p_data, p_store->scratch, selected.size);
    *p_record = selected;
    return CFGSTORE_OK;
}

/** @brief Identical retries do not erase; older or conflicting revisions fail.
 */
static int write_record (cfgstore_t *    p_store,
                         const uint8_t * p_data,
                         size_t          size,
                         uint32_t        revision)
{
    cfgstore_record_t selected = {0};
    const int         found    = select_record(p_store, &selected);
    if ((CFGSTORE_OK != found) && (CFGSTORE_EMPTY != found))
    {
        return found;
    }
    if ((CFGSTORE_OK == found) && (revision <= selected.revision))
    {
        cfgstore_record_t verified = {0};
        if ((revision != selected.revision) || (size != selected.size))
        {
            return CFGSTORE_CONFLICT;
        }
        if ((CFGSTORE_OK != inspect_slot(p_store, selected.slot, &verified)) ||
            (verified.revision != selected.revision) ||
            (verified.size != selected.size) || (verified.crc != selected.crc))
        {
            return CFGSTORE_IO;
        }
        return (0 == memcmp(p_store->scratch, p_data, size))
                   ? CFGSTORE_OK
                   : CFGSTORE_CONFLICT;
    }
    const unsigned slot = (CFGSTORE_OK == found) ? (1U - selected.slot) : 0U;
    return commit_record(p_store, slot, p_data, size, revision);
}

/** @brief Verify uncommitted bytes, then write/sync the separate commit marker.
 */
static int commit_record (cfgstore_t *    p_store,
                          unsigned        slot,
                          const uint8_t * p_data,
                          size_t          size,
                          uint32_t        revision)
{
    const cfgstore_ops_t * p_ops = p_store->p_operations;
    uint8_t                header[UTHOME_CFGSTORE_HEADER] = {0};
    memset(header, 0xFF, sizeof(header));
    cfgbytes_put(header, g_magic);
    cfgbytes_put(&header[4], 1U);
    cfgbytes_put(&header[8], revision);
    /* save bounds size to 8192, representable in the wire field. */
    cfgbytes_put(&header[12], (uint32_t)size);
    cfgbytes_put(&header[16], ~cfgbytes_crc(UINT32_MAX, p_data, size));
    cfgbytes_put(&header[20], ~cfgbytes_crc(UINT32_MAX, header, 20U));
    if ((0 != p_ops->p_erase(p_store->p_context, slot)) ||
        (0 != p_ops->p_sync(p_store->p_context)) ||
        (0 != p_ops->p_write(p_store->p_context, slot, 0U, header, 24U)) ||
        (0 !=
         p_ops->p_write(
             p_store->p_context, slot, UTHOME_CFGSTORE_HEADER, p_data, size)) ||
        (0 != p_ops->p_sync(p_store->p_context)))
    {
        return CFGSTORE_IO;
    }
    uint8_t verified[UTHOME_CFGSTORE_HEADER] = {0};
    if ((0 != p_ops->p_read(
                  p_store->p_context, slot, 0U, verified, sizeof(verified))) ||
        (0 != memcmp(header, verified, sizeof(header))) ||
        (0 != p_ops->p_read(p_store->p_context,
                            slot,
                            UTHOME_CFGSTORE_HEADER,
                            p_store->scratch,
                            size)) ||
        (0 != memcmp(p_data, p_store->scratch, size)))
    {
        return CFGSTORE_IO;
    }
    cfgbytes_put(&header[24], g_commit);
    if ((0 != p_ops->p_write(p_store->p_context, slot, 24U, &header[24], 4U)) ||
        (0 != p_ops->p_sync(p_store->p_context)))
    {
        return CFGSTORE_IO;
    }
    cfgstore_record_t record = {0};
    if ((CFGSTORE_OK != inspect_slot(p_store, slot, &record)) ||
        (record.revision != revision) || (record.size != size) ||
        (0 != memcmp(p_data, p_store->scratch, size)))
    {
        return CFGSTORE_IO;
    }
    return CFGSTORE_OK;
}

/* End of file. */

