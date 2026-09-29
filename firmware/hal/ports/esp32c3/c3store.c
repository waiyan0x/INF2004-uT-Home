/**
 * @file c3store.c
 * @brief Synchronous partition operations with checked slot isolation.
 */
#include "c3store.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "workport.h"
static const esp_partition_t * gp_partition = NULL;
static int read_slot (void *    p_context,
                      unsigned  slot,
                      size_t    offset,
                      uint8_t * p_data,
                      size_t    size);
static int write_slot (void *          p_context,
                       unsigned        slot,
                       size_t          offset,
                       const uint8_t * p_data,
                       size_t          size);
static int erase_slot (void * p_context, unsigned slot);
static int sync_slots (void * p_context);
static bool
valid_range (void * p_context, unsigned slot, size_t offset, size_t size);
static const cfgstore_ops_t g_operations = {.p_read  = read_slot,
                                            .p_write = write_slot,
                                            .p_erase = erase_slot,
                                            .p_sync  = sync_slots};

/** @brief Select only the layout built for this application; never format it.
 */
int c3store_open (void)
{
    if ((!rt_port_is_owner()) || (NULL != gp_partition))
    {
        return -1;
    }
    /* Application-defined type 0x40 is within the SDK's documented 8-bit range.
     */
    const esp_partition_t * p_partition = esp_partition_find_first(
        (esp_partition_type_t)0x40U, (esp_partition_subtype_t)0U, "uthome_cfg");
    if ((NULL == p_partition) ||
        (esp_flash_default_chip != p_partition->flash_chip) ||
        (0x110000U != p_partition->address) ||
        ((2U * UTHOME_C3STORE_SLOT) != p_partition->size) ||
        (4096U != p_partition->erase_size) || (p_partition->encrypted) ||
        (p_partition->readonly))
    {
        return -1;
    }
    gp_partition = p_partition;
    return 0;
}

/** @brief Return callbacks even if the partition is unavailable. */
const cfgstore_ops_t * c3store_operations (void)
{
    return &g_operations;
}

/** @brief Check subtraction before addition so hostile sizes cannot wrap. */
static bool
valid_range (void * p_context, unsigned slot, size_t offset, size_t size)
{
    return (NULL == p_context) && (NULL != gp_partition) &&
           (rt_port_is_owner()) && (slot < 2U) &&
           (offset <= UTHOME_C3STORE_SLOT) &&
           (size <= (UTHOME_C3STORE_SLOT - offset));
}

/** @brief Partition API also bounds the translated range. */
static int read_slot (void *    p_context,
                      unsigned  slot,
                      size_t    offset,
                      uint8_t * p_data,
                      size_t    size)
{
    if ((NULL == p_data) || (!valid_range(p_context, slot, offset, size)))
    {
        return -1;
    }
    return (ESP_OK == esp_partition_read(gp_partition,
                                         (slot * UTHOME_C3STORE_SLOT) + offset,
                                         p_data,
                                         size))
               ? 0
               : -1;
}

/** @brief The SDK programs exact unencrypted byte ranges synchronously. */
static int write_slot (void *          p_context,
                       unsigned        slot,
                       size_t          offset,
                       const uint8_t * p_data,
                       size_t          size)
{
    if ((NULL == p_data) || (!valid_range(p_context, slot, offset, size)))
    {
        return -1;
    }
    return (ESP_OK == esp_partition_write(gp_partition,
                                          (slot * UTHOME_C3STORE_SLOT) + offset,
                                          p_data,
                                          size))
               ? 0
               : -1;
}

/** @brief Erase exactly three sectors in the selected slot, never its peer. */
static int erase_slot (void * p_context, unsigned slot)
{
    if (!valid_range(p_context, slot, 0U, UTHOME_C3STORE_SLOT))
    {
        return -1;
    }
    return (ESP_OK == esp_partition_erase_range(gp_partition,
                                                slot * UTHOME_C3STORE_SLOT,
                                                UTHOME_C3STORE_SLOT))
               ? 0
               : -1;
}

/** @brief No pending buffer: SDK operations complete before they return. */
static int sync_slots (void * p_context)
{
    return valid_range(p_context, 0U, 0U, 0U) ? 0 : -1;
}

/* End of file. */

