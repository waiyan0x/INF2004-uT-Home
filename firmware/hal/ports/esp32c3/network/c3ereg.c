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
#include <stdlib.h>
#include <string.h>

#if !CONFIG_ESP_EVENT_POST_FROM_ISR || CONFIG_ESP_EVENT_LOOP_PROFILING ||      \
    CONFIG_ESP_EVENT_LOOP_IN_EXT_RAM
#error "Unsupported SDK event memory configuration"
#endif

/* One loop node, six event IDs, eight handlers and a provisional handler. */
#define C3EREG_NODES 16U
#define C3EREG_BASES 3U
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

static c3ereg_pool_t const g_pools[] = {
    {&g_loop, &gb_loop, 1U, sizeof(g_loop)},
    {&g_bases, gb_bases, C3EREG_BASES, sizeof(g_bases[0U])},
    {&g_nodes, gb_nodes, C3EREG_NODES, sizeof(g_nodes[0U])},
    {&g_contexts, gb_contexts, C3EREG_CONTEXTS, sizeof(g_contexts[0U])}};

/** @brief Lease one zeroed, naturally aligned SDK record. */
void * c3ereg_alloc (size_t count, size_t size, uint32_t caps)
{
    if ((1U != count) || (MALLOC_CAP_DEFAULT != caps))
    {
        return NULL;
    }
    void *         p_result = NULL;
    const uint32_t saved    = c3bport_enter();
    for (size_t pool = 0U; pool < (sizeof(g_pools) / sizeof(g_pools[0U]));
         ++pool)
    {
        c3ereg_pool_t const * const p_pool = &g_pools[pool];
        if (size == p_pool->size)
        {
            uint8_t * const p_bytes = p_pool->p_records;
            for (size_t slot = 0U; slot < p_pool->count; ++slot)
            {
                if (!p_pool->pb_used[slot])
                {
                    p_result = &p_bytes[slot * size];
                    memset(p_result, 0, size);
                    p_pool->pb_used[slot] = true;
                    break;
                }
            }
            break;
        }
    }
    c3bport_leave(saved);
    return p_result;
}

/** @brief Release exact record addresses; SDK posted payloads use libc. */
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
    free(p_memory);
}
#endif

/* End of file. */

