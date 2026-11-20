/**
 * @file c3lrec.c
 * @brief Bound lwIP record leases without allocating tasks, locks or heap.
 */
#include "c3lrec.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "c3bport.h"
#include "lwip/api.h"
#include "lwip/etharp.h"
#include "lwip/igmp.h"
#include "lwip/netbuf.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/priv/tcpip_priv.h"
#include "lwip/stats.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Two unresolved neighbours plus one append during the queue-limit check. */
#define C3LREC_ARP_SLOTS ((2U * ARP_QUEUE_LEN) + 1U)

/* The one-broker station uses only the IPv4 all-systems membership. */
#define C3LREC_GROUP_SLOTS 1U

/* Two cyclic timers, seven on-demand owners and three spare records. */
#define C3LREC_TIMER_SLOTS 12U

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

static struct tcpip_msg g_inputs[MEMP_NUM_TCPIP_MSG_INPKT]   = {0};
static struct tcpip_msg g_callbacks[MEMP_NUM_TCPIP_MSG_API]  = {0};
static bool             gb_inputs[MEMP_NUM_TCPIP_MSG_INPKT]  = {false};
static bool             gb_callbacks[MEMP_NUM_TCPIP_MSG_API] = {false};

static struct udp_pcb        g_datagrams[MEMP_NUM_UDP_PCB]  = {0};
static struct etharp_q_entry g_pending[C3LREC_ARP_SLOTS]    = {0};
static bool                  gb_datagrams[MEMP_NUM_UDP_PCB] = {false};
static bool                  gb_pending[C3LREC_ARP_SLOTS]   = {false};

static struct igmp_group g_groups[C3LREC_GROUP_SLOTS]  = {0};
static bool              gb_groups[C3LREC_GROUP_SLOTS] = {false};

static struct sys_timeo g_timers[C3LREC_TIMER_SLOTS]  = {0};
static bool             gb_timers[C3LREC_TIMER_SLOTS] = {false};

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
     sizeof(g_segments[0])},
    {MEMP_TCPIP_MSG_INPKT,
     &g_inputs,
     gb_inputs,
     MEMP_NUM_TCPIP_MSG_INPKT,
     sizeof(g_inputs[0])},
    {MEMP_TCPIP_MSG_API,
     &g_callbacks,
     gb_callbacks,
     MEMP_NUM_TCPIP_MSG_API,
     sizeof(g_callbacks[0])},
    {MEMP_UDP_PCB,
     &g_datagrams,
     gb_datagrams,
     MEMP_NUM_UDP_PCB,
     sizeof(g_datagrams[0])},
    {MEMP_ARP_QUEUE,
     &g_pending,
     gb_pending,
     C3LREC_ARP_SLOTS,
     sizeof(g_pending[0])},
    {MEMP_IGMP_GROUP,
     &g_groups,
     gb_groups,
     C3LREC_GROUP_SLOTS,
     sizeof(g_groups[0])},
    {MEMP_SYS_TIMEOUT,
     &g_timers,
     gb_timers,
     C3LREC_TIMER_SLOTS,
     sizeof(g_timers[0])}};

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

