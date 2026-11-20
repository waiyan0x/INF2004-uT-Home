/**
 * @file c3lrec.c
 * @brief Bound lwIP record leases without allocating tasks, locks or heap.
 */
#include "c3lrec.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "c3bport.h"
#include "lwip/api.h"
#include "lwip/netbuf.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/stats.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef struct
{
    memp_t kind;
    void * p_records;
    bool * pb_used;
    size_t count;
    size_t size;
} c3lrec_pool_t;

static c3lrec_pool_t const * find_pool (memp_t kind);

static struct netconn        g_connections[MEMP_NUM_NETCONN]       = {0};
static struct netbuf         g_buffers[MEMP_NUM_NETBUF]            = {0};
static struct tcp_pcb        g_active[MEMP_NUM_TCP_PCB]            = {0};
static struct tcp_pcb_listen g_listeners[MEMP_NUM_TCP_PCB_LISTEN]  = {0};
static struct tcp_seg        g_segments[MEMP_NUM_TCP_SEG]          = {0};
static bool                  gb_connections[MEMP_NUM_NETCONN]      = {false};
static bool                  gb_buffers[MEMP_NUM_NETBUF]           = {false};
static bool                  gb_active[MEMP_NUM_TCP_PCB]           = {false};
static bool                  gb_listeners[MEMP_NUM_TCP_PCB_LISTEN] = {false};
static bool                  gb_segments[MEMP_NUM_TCP_SEG]         = {false};

/* Each base denotes the complete typed array, including all its elements. */
static c3lrec_pool_t const g_pools[] = {
    {MEMP_NETCONN,
     &g_connections,
     gb_connections,
     MEMP_NUM_NETCONN,
     sizeof(g_connections[0])},
    {MEMP_NETBUF,
     &g_buffers,
     gb_buffers,
     MEMP_NUM_NETBUF,
     sizeof(g_buffers[0])},
    {MEMP_TCP_PCB, &g_active, gb_active, MEMP_NUM_TCP_PCB, sizeof(g_active[0])},
    {MEMP_TCP_PCB_LISTEN,
     &g_listeners,
     gb_listeners,
     MEMP_NUM_TCP_PCB_LISTEN,
     sizeof(g_listeners[0])},
    {MEMP_TCP_SEG,
     &g_segments,
     gb_segments,
     MEMP_NUM_TCP_SEG,
     sizeof(g_segments[0])}};

/** @brief Lease a zeroed, naturally aligned record of the requested SDK type.
 */
void * c3lrec_alloc (memp_t kind)
{
    c3lrec_pool_t const * const p_pool   = find_pool(kind);
    void *                      p_result = NULL;
    const uint32_t              saved    = c3bport_enter();
    if (NULL != p_pool)
    {
        uint8_t * const p_bytes = p_pool->p_records;
        for (size_t index = 0U; index < p_pool->count; ++index)
        {
            if (!p_pool->pb_used[index])
            {
                p_result = &p_bytes[index * p_pool->size];
                memset(p_result, 0, p_pool->size);
                p_pool->pb_used[index] = true;
                break;
            }
        }
#if MEMP_STATS
        /* Native TCP can discount failed probes after reclaiming a PCB. */
        struct stats_mem * const p_stats = lwip_stats.memp[kind];
        if (NULL == p_result)
        {
            ++p_stats->err;
        }
        else
        {
            ++p_stats->used;
            if (p_stats->used > p_stats->max)
            {
                p_stats->max = p_stats->used;
            }
        }
#endif
    }
    c3bport_leave(saved);
    return p_result;
}

/** @brief Return only an exact record address from the pool of that kind. */
void c3lrec_free (memp_t kind, void * p_memory)
{
    c3lrec_pool_t const * const p_pool = find_pool(kind);
    const uint32_t              saved  = c3bport_enter();
    if (NULL != p_pool)
    {
        uint8_t * const p_bytes = p_pool->p_records;
        for (size_t index = 0U; index < p_pool->count; ++index)
        {
            if ((p_pool->pb_used[index]) &&
                (p_memory == &p_bytes[index * p_pool->size]))
            {
                memset(p_memory, 0, p_pool->size);
                p_pool->pb_used[index] = false;
#if MEMP_STATS
                MEMP_STATS_DEC(used, kind);
#endif
                break;
            }
        }
    }
    c3bport_leave(saved);
}

/** @brief Map a native pool identifier to its immutable layout. */
static c3lrec_pool_t const * find_pool (memp_t kind)
{
    for (size_t index = 0U; index < (sizeof(g_pools) / sizeof(g_pools[0]));
         ++index)
    {
        if (g_pools[index].kind == kind)
        {
            return &g_pools[index];
        }
    }
    return NULL;
}

#endif

/* End of file. */

