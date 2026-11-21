/**
 * @file c3iface.c
 * @brief Keep the station and its IP records in fixed, typed storage.
 */
#include "c3iface.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "c3bport.h"
#include "esp_netif_lwip_internal.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* The pinned station setter admits at most 32 hostname characters. */
#define C3IFACE_HOST_BYTES 33U

typedef struct
{
    void * p_storage;
    size_t size;
} c3iface_slot_t;

static struct esp_netif_obj g_station                      = {0};
static struct netif         g_interface                    = {0};
static esp_netif_ip_info_t  g_addresses[2U]                = {0};
static char                 g_desc[sizeof("sta")]          = {0};
static char                 g_key[sizeof("WIFI_STA_DEF")]  = {0};
static char                 g_hostname[C3IFACE_HOST_BYTES] = {0};

static c3iface_slot_t const g_records[] = {
    {&g_station, sizeof(g_station)},
    {&g_interface, sizeof(g_interface)},
    {&g_addresses[0U], sizeof(g_addresses[0U])},
    {&g_addresses[1U], sizeof(g_addresses[1U])}};
static c3iface_slot_t const g_strings[] = {{g_desc, sizeof(g_desc)},
                                           {g_key, sizeof(g_key)},
                                           {g_hostname, sizeof(g_hostname)}};
#define C3IFACE_RECORDS (sizeof(g_records) / sizeof(g_records[0U]))
#define C3IFACE_STRINGS (sizeof(g_strings) / sizeof(g_strings[0U]))
static bool gb_records[C3IFACE_RECORDS] = {false};
static bool gb_strings[C3IFACE_STRINGS] = {false};

/** @brief Lease one zeroed object of the exact native constructor size. */
void * c3iface_alloc (size_t count, size_t size)
{
    void * p_result = NULL;
    if (1U == count)
    {
        const uint32_t saved = c3bport_enter();
        for (size_t index = 0U; index < C3IFACE_RECORDS; ++index)
        {
            if ((!gb_records[index]) && (size == g_records[index].size))
            {
                p_result = g_records[index].p_storage;
                memset(p_result, 0, size);
                gb_records[index] = true;
                break;
            }
        }
        c3bport_leave(saved);
    }
    return p_result;
}

/** @brief Copy the station key, description or hostname into its own lease. */
char * c3iface_dup (char const * p_text)
{
    char *         p_result = NULL;
    const size_t   length   = strlen(p_text);
    const uint32_t saved    = c3bport_enter();
    for (size_t index = 0U; index < C3IFACE_STRINGS; ++index)
    {
        if ((!gb_strings[index]) && (length < g_strings[index].size))
        {
            p_result = g_strings[index].p_storage;
            memcpy(p_result, p_text, length + 1U);
            gb_strings[index] = true;
            break;
        }
    }
    c3bport_leave(saved);
    return p_result;
}

/** @brief Release only a matching object or string from this SDK unit. */
void c3iface_free (void * p_memory)
{
    const uint32_t saved = c3bport_enter();
    for (size_t index = 0U; index < C3IFACE_RECORDS; ++index)
    {
        if (p_memory == g_records[index].p_storage)
        {
            gb_records[index] = false;
            break;
        }
    }
    for (size_t index = 0U; index < C3IFACE_STRINGS; ++index)
    {
        if (p_memory == g_strings[index].p_storage)
        {
            gb_strings[index] = false;
            break;
        }
    }
    c3bport_leave(saved);
}
#endif

/* End of file. */

