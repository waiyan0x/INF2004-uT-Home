/**
 * @file c3store.c
 * @brief Synchronous partition operations with checked slot isolation.
 */
#include "c3store.h"
#include "esp_partition.h"
#include "esp_flash.h"
#include "workport.h"
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
static bool is_owner (void * p_context);
static int flash_size (void * p_context, uint32_t * p_size);
static int
read_table (void * p_context, size_t offset, uint8_t * p_data, size_t size);
static int
read_partition (void * p_context, size_t offset, uint8_t * p_data, size_t size);
static int write_partition (void *          p_context,
                            size_t          offset,
                            const uint8_t * p_data,
                            size_t          size);
static int erase_partition (void * p_context, size_t offset, size_t size);
static const cfgslot_ops_t  g_flash      = {.p_owner = is_owner,
                                            .p_size  = flash_size,
                                            .p_table = read_table,
                                            .p_read  = read_partition,
                                            .p_write = write_partition,
                                            .p_erase = erase_partition};
static cfgslot_t            g_state      = {.p_operations = &g_flash,
                                            .p_context    = &g_partition};
static const cfgstore_ops_t g_operations = {.p_read  = read_slot,
                                            .p_write = write_slot,
                                            .p_erase = erase_slot,
                                            .p_sync  = sync_slots};

/** @brief Select only the layout built for this application; never format it.
 */
int c3store_open (void)
{
    const int result = cfgslot_open(&g_state);
    if (0 == result)
    {
        g_partition.flash_chip = esp_flash_default_chip;
    }
    return result;
}

/** @brief Preserve both flash guards with a narrower verified write window. */
bool c3store_region_allowed (size_t address, size_t size)
{
    return cfgslot_region_allowed(&g_state, address, size);
}

/** @brief Return callbacks even if the partition is unavailable. */
const cfgstore_ops_t * c3store_operations (void)
{
    return &g_operations;
}

/** @brief Keep the controller's null context contract at the SDK boundary. */
static int read_slot (void *    p_context,
                      unsigned  slot,
                      size_t    offset,
                      uint8_t * p_data,
                      size_t    size)
{
    return (NULL == p_context)
               ? cfgslot_read(&g_state, slot, offset, p_data, size)
               : -1;
}

/** @brief Forward only the checked, exact byte range to the partition API. */
static int write_slot (void *          p_context,
                       unsigned        slot,
                       size_t          offset,
                       const uint8_t * p_data,
                       size_t          size)
{
    return (NULL == p_context)
               ? cfgslot_write(&g_state, slot, offset, p_data, size)
               : -1;
}

/** @brief Erase exactly one slot after portable bounds/owner checks. */
static int erase_slot (void * p_context, unsigned slot)
{
    return (NULL == p_context) ? cfgslot_erase(&g_state, slot) : -1;
}

/** @brief SDK operations complete before returning; no flush is pending. */
static int sync_slots (void * p_context)
{
    return (NULL == p_context) ? cfgslot_sync(&g_state) : -1;
}

/** @brief All storage operations belong to the serialized controller task. */
static bool is_owner (void * p_context)
{
    (void)p_context; /* Ownership describes the calling task, not the partition.
                      */
    return rt_port_is_owner();
}

/** @brief Require the SDK's internal flash chip before inspecting its size. */
static int flash_size (void * p_context, uint32_t * p_size)
{
    (void)
        p_context; /* The SDK default chip is independent of the descriptor. */
    return ((NULL != esp_flash_default_chip) &&
            (ESP_OK == esp_flash_get_size(esp_flash_default_chip, p_size)))
               ? 0
               : -1;
}

/** @brief Read only bounded physical table chunks into internal DRAM. */
static int
read_table (void * p_context, size_t offset, uint8_t * p_data, size_t size)
{
    (void)
        p_context; /* The descriptor becomes usable only after verification. */
    /* cfgslot restricts offset/size to the 3072-byte table and 256-byte chunk,
     * both within the SDK's uint32 address/length range. */
    return (ESP_OK == esp_flash_read(esp_flash_default_chip,
                                     p_data,
                                     (uint32_t)offset,
                                     (uint32_t)size))
               ? 0
               : -1;
}

/** @brief Adapt an already bounded partition read; no alternate buffer. */
static int
read_partition (void * p_context, size_t offset, uint8_t * p_data, size_t size)
{
    const esp_partition_t * p_partition = p_context;
    return (ESP_OK == esp_partition_read(p_partition, offset, p_data, size))
               ? 0
               : -1;
}

/** @brief Program exact unencrypted bytes synchronously. */
static int write_partition (void *          p_context,
                            size_t          offset,
                            const uint8_t * p_data,
                            size_t          size)
{
    const esp_partition_t * p_partition = p_context;
    return (ESP_OK == esp_partition_write(p_partition, offset, p_data, size))
               ? 0
               : -1;
}

/** @brief The portable core supplies exactly one aligned slot. */
static int erase_partition (void * p_context, size_t offset, size_t size)
{
    const esp_partition_t * p_partition = p_context;
    return (ESP_OK == esp_partition_erase_range(p_partition, offset, size))
               ? 0
               : -1;
}

/* End of file. */

