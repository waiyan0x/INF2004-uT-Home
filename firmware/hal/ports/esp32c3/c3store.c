/**
 * @file c3store.c
 * @brief Synchronous partition operations with checked slot isolation.
 */
#include "c3store.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "workport.h"
#include <string.h>
static bool    gb_ready       = false;
static uint8_t g_scratch[256] = {0};
/* These external enum casts preserve documented custom 8-bit partition IDs. */
static esp_partition_t g_partition = {.flash_chip = NULL,
                                      .type       = (esp_partition_type_t)0x40U,
                                      .subtype    = (esp_partition_subtype_t)0U,
                                      .address    = UTHOME_PART_CONFIG_OFFSET,
                                      .size       = UTHOME_PART_CONFIG_BYTES,
                                      .erase_size = 4096U,
                                      .label      = "uthome_cfg",
                                      .encrypted  = false,
                                      .readonly   = false};
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
    if ((!rt_port_is_owner()) || gb_ready)
    {
        return -1;
    }
    uint32_t flash_size = 0U;
    if ((NULL == esp_flash_default_chip) ||
        (ESP_OK != esp_flash_get_size(esp_flash_default_chip, &flash_size)) ||
        (UTHOME_PART_FLASH_BYTES != flash_size))
    {
        return -1;
    }
    for (uint32_t offset = 0U; offset < UTHOME_PART_TABLE_BYTES;
         offset += sizeof(g_scratch))
    {
        /* The complete 3072-byte table is a multiple of this fixed chunk.
         * Internal DRAM avoids the SDK's allocated read-bounce fallback. */
        if ((ESP_OK != esp_flash_read(esp_flash_default_chip,
                                      g_scratch,
                                      UTHOME_PART_TABLE_OFFSET + offset,
                                      sizeof(g_scratch))) ||
            (!cfgpart_check(g_scratch, offset, sizeof(g_scratch))))
        {
            return -1;
        }
    }
    g_partition.flash_chip = esp_flash_default_chip;
    gb_ready               = true;
    return 0;
}

/** @brief Preserve both flash guards with a narrower verified write window. */
bool c3store_region_allowed (size_t address, size_t size)
{
    return gb_ready && (cfgpart_writable(address, size));
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
    return (NULL == p_context) && gb_ready && (rt_port_is_owner()) &&
           (slot < 2U) && (offset <= UTHOME_C3STORE_SLOT) &&
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
    for (size_t done = 0U; done < size;)
    {
        const size_t remaining = size - done;
        const size_t chunk =
            (remaining < sizeof(g_scratch)) ? remaining : sizeof(g_scratch);
        if (ESP_OK !=
            esp_partition_read(&g_partition,
                               (slot * UTHOME_C3STORE_SLOT) + offset + done,
                               g_scratch,
                               chunk))
        {
            return -1;
        }
        memcpy(&p_data[done], g_scratch, chunk);
        done += chunk;
    }
    return 0;
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
    return (ESP_OK == esp_partition_write(&g_partition,
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
    return (ESP_OK == esp_partition_erase_range(&g_partition,
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

