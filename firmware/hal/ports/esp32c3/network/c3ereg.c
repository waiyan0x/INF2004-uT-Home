/**
 * @file c3ereg.c
 * @brief Store the single demo event loop and its registration tree.
 */
#include "c3ereg.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "c3bport.h"
#include "esp_heap_caps.h"
#include "esp_event_internal.h"
#include <stdbool.h>
#include "esp_wifi_types.h"
#include "esp_netif_types.h"
#include <string.h>

#if !CONFIG_ESP_EVENT_POST_FROM_ISR || CONFIG_ESP_EVENT_LOOP_PROFILING ||      \
    CONFIG_ESP_EVENT_LOOP_IN_EXT_RAM
#error "Unsupported SDK event memory configuration"
#endif

/* One loop node, six event IDs, eight handlers and a provisional handler. */
#define C3EREG_NODES    16U
#define C3EREG_BASES    3U
#define C3EREG_REMOVALS 8U
#define C3EREG_MESSAGES 2U
/* Eight live contexts, eight deferred removals and a provisional context. */
#define C3EREG_CONTEXTS 17U

typedef union
{
    esp_event_loop_node_t    level;
    esp_event_id_node_t      identifier;
    esp_event_handler_node_t handler;
} c3ereg_node_t;

/* The SDK requests these three native types through the same size-only ABI. */
#define C3EREG_SAME_SIZE                                                       \
    ((sizeof(c3ereg_node_t) == sizeof(esp_event_loop_node_t)) &&               \
     (sizeof(c3ereg_node_t) == sizeof(esp_event_id_node_t)) &&                 \
     (sizeof(c3ereg_node_t) == sizeof(esp_event_handler_node_t)))
typedef char c3ereg_layout_t[C3EREG_SAME_SIZE ? 1 : -1];

typedef struct
{
    void * p_records;
    bool * pb_used;
    size_t count;
    size_t size;
} c3ereg_pool_t;

static esp_event_loop_instance_t            g_loop                      = {0};
static esp_event_base_node_t                g_bases[C3EREG_BASES]       = {0};
static c3ereg_node_t                        g_nodes[C3EREG_NODES]       = {0};
static esp_event_handler_instance_context_t g_contexts[C3EREG_CONTEXTS] = {0};
static bool                                 gb_loop                     = false;
static bool                                 gb_bases[C3EREG_BASES] = {false};
static bool                                 gb_nodes[C3EREG_NODES] = {false};
static bool gb_contexts[C3EREG_CONTEXTS]                           = {false};

typedef union
{
    wifi_event_sta_connected_t       connected;
    wifi_event_sta_disconnected_t    disconnected;
    wifi_event_home_channel_change_t channel;
    ip_event_got_ip_t                address;
    uint64_t                         alignment;
} c3ereg_body_t;

static esp_event_remove_handler_context_t g_cleanup[C3EREG_REMOVALS]  = {0};
static c3ereg_body_t                      g_messages[C3EREG_MESSAGES] = {0};
static bool                               gb_cleanup[C3EREG_REMOVALS] = {false};
static bool gb_messages[C3EREG_MESSAGES]                              = {false};

static c3ereg_pool_t const g_pools[] = {
    {&g_loop, &gb_loop, 1U, sizeof(g_loop)},
    {&g_bases, gb_bases, C3EREG_BASES, sizeof(g_bases[0U])},
    {&g_nodes, gb_nodes, C3EREG_NODES, sizeof(g_nodes[0U])},
    {&g_contexts, gb_contexts, C3EREG_CONTEXTS, sizeof(g_contexts[0U])},
    {g_cleanup, gb_cleanup, C3EREG_REMOVALS, sizeof(g_cleanup[0U])},
    {g_messages, gb_messages, C3EREG_MESSAGES, sizeof(g_messages[0U])}};

#define C3EREG_RECORD_POOLS 4U

/** @brief Claim one empty slot with interrupts excluded by the caller. */
static void * lease (c3ereg_pool_t const * p_pool)
{
    uint8_t * const p_bytes = p_pool->p_records;
    for (size_t slot = 0U; slot < p_pool->count; ++slot)
    {
        if (!p_pool->pb_used[slot])
        {
            void * const p_result = &p_bytes[slot * p_pool->size];
            memset(p_result, 0, p_pool->size);
            p_pool->pb_used[slot] = true;
            return p_result;
        }
    }
    return NULL;
}

/** @brief Keep all eight handler removals possible during station traffic. */
void * c3ereg_body (size_t size, bool b_cleanup)
{
    c3ereg_pool_t const * const p_pool =
        &g_pools[b_cleanup ? C3EREG_RECORD_POOLS : (C3EREG_RECORD_POOLS + 1U)];
    if ((0U == size) || (size > p_pool->size) ||
        (b_cleanup && (size != sizeof(g_cleanup[0U]))))
    {
        return NULL;
    }
    const uint32_t saved  = c3bport_enter();
    void * const   p_body = lease(p_pool);
    c3bport_leave(saved);
    return p_body;
}

/** @brief Lease one zeroed, naturally aligned SDK record. */
void * c3ereg_alloc (size_t count, size_t size, uint32_t caps)
{
    if ((1U != count) || (MALLOC_CAP_DEFAULT != caps))
    {
        return NULL;
    }
    void *         p_result = NULL;
    const uint32_t saved    = c3bport_enter();
    for (size_t pool = 0U; pool < C3EREG_RECORD_POOLS; ++pool)
    {
        if (size == g_pools[pool].size)
        {
            p_result = lease(&g_pools[pool]);
            break;
        }
    }
    c3bport_leave(saved);
    return p_result;
}

/** @brief Release an exact registration or posted-body slot address. */
void c3ereg_free (void * p_memory)
{
    const uint32_t saved = c3bport_enter();
    for (size_t pool = 0U; pool < (sizeof(g_pools) / sizeof(g_pools[0U]));
         ++pool)
    {
        c3ereg_pool_t const * const p_pool  = &g_pools[pool];
        uint8_t const * const       p_bytes = p_pool->p_records;
        for (size_t slot = 0U; slot < p_pool->count; ++slot)
        {
            if (p_memory == &p_bytes[slot * p_pool->size])
            {
                p_pool->pb_used[slot] = false;
                c3bport_leave(saved);
                return;
            }
        }
    }
    c3bport_leave(saved);
}
#endif

/* End of file. */

