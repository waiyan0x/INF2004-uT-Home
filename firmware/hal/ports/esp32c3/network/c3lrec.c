/**
 * @file c3lrec.c
 * @brief Bound lwIP record leases without allocating tasks, locks or heap.
 */
#include "c3lrec.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "c3bport.h"
#include "lwip/api.h"
#include "lwip/netbuf.h"
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static struct netconn g_connections[MEMP_NUM_NETCONN]  = {0};
static struct netbuf  g_buffers[MEMP_NUM_NETBUF]       = {0};
static bool           gb_connections[MEMP_NUM_NETCONN] = {false};
static bool           gb_buffers[MEMP_NUM_NETBUF]      = {false};

/** @brief Lease actual SDK types with their native alignment and lifetime. */
void * c3lrec_alloc (memp_t kind)
{
    void *         p_result = NULL;
    const uint32_t saved    = c3bport_enter();
    if (MEMP_NETCONN == kind)
    {
        for (size_t index = 0U; index < MEMP_NUM_NETCONN; ++index)
        {
            if (!gb_connections[index])
            {
                memset(&g_connections[index], 0, sizeof(g_connections[index]));
                gb_connections[index] = true;
                p_result              = &g_connections[index];
                break;
            }
        }
    }
    else if (MEMP_NETBUF == kind)
    {
        for (size_t index = 0U; index < MEMP_NUM_NETBUF; ++index)
        {
            if (!gb_buffers[index])
            {
                memset(&g_buffers[index], 0, sizeof(g_buffers[index]));
                gb_buffers[index] = true;
                p_result          = &g_buffers[index];
                break;
            }
        }
    }
    else
    {
        /* Other pool kinds retain their separate SDK allocation paths. */
    }
    c3bport_leave(saved);
    return p_result;
}

/** @brief Return only a record of the requested kind, after SDK cleanup. */
void c3lrec_free (memp_t kind, void * p_memory)
{
    const uint32_t saved = c3bport_enter();
    if (MEMP_NETCONN == kind)
    {
        for (size_t index = 0U; index < MEMP_NUM_NETCONN; ++index)
        {
            if (p_memory == &g_connections[index])
            {
                memset(&g_connections[index], 0, sizeof(g_connections[index]));
                gb_connections[index] = false;
                break;
            }
        }
    }
    else if (MEMP_NETBUF == kind)
    {
        for (size_t index = 0U; index < MEMP_NUM_NETBUF; ++index)
        {
            if (p_memory == &g_buffers[index])
            {
                memset(&g_buffers[index], 0, sizeof(g_buffers[index]));
                gb_buffers[index] = false;
                break;
            }
        }
    }
    else
    {
        /* Unsupported kinds, NULL and foreign pointers preserve every lease. */
    }
    c3bport_leave(saved);
}

#endif

/* End of file. */

