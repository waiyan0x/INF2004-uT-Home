/**
 * @file c3dhcp.c
 * @brief Keep station DHCP traffic independent of TCP packet leases.
 */
#include "c3dhcp.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "c3bport.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define C3DHCP_PACKETS 2U

typedef struct
{
    struct pbuf_custom header;
    uint32_t words[LWIP_MEM_ALIGN_SIZE(LWIP_MEM_ALIGN_SIZE(PBUF_TRANSPORT) +
                                       sizeof(struct dhcp_msg)) /
                   sizeof(uint32_t)];
} c3dhcp_packet_t;

static void release (struct pbuf * p_packet);

static struct dhcp     g_state                   = {0};
static bool            gb_used                   = false;
static c3dhcp_packet_t g_packets[C3DHCP_PACKETS] = {0};

/** @brief The serialized DHCP core initializes the lease before each start. */
void * c3dhcp_alloc (mem_size_t size)
{
    void * p_state = NULL;
    if ((!gb_used) && (sizeof(g_state) == size))
    {
        gb_used = true;
        p_state = &g_state;
    }
    return p_state;
}

/** @brief Native cleanup detaches this state from its interface. */
void c3dhcp_free (void * p_memory)
{
    if (&g_state == p_memory)
    {
        gb_used = false;
    }
}

/** @brief A retained unicast and a new broadcast can hold separate packets. */
struct pbuf * c3dhcp_packet (pbuf_layer layer, uint16_t length, pbuf_type type)
{
    if ((PBUF_TRANSPORT != layer) || (PBUF_RAM != type) ||
        (sizeof(struct dhcp_msg) != length))
    {
        return NULL;
    }
    struct pbuf *  p_packet = NULL;
    const uint32_t saved    = c3bport_enter();
    for (size_t index = 0U; index < C3DHCP_PACKETS; ++index)
    {
        c3dhcp_packet_t * const p_slot = &g_packets[index];
        if (NULL == p_slot->header.custom_free_function)
        {
            p_packet = pbuf_alloced_custom(layer,
                                           length,
                                           type,
                                           &p_slot->header,
                                           p_slot->words,
                                           sizeof(p_slot->words));
            if (NULL != p_packet)
            {
                p_slot->header.custom_free_function = release;
            }
            break;
        }
    }
    c3bport_leave(saved);
    return p_packet;
}

/** @brief lwIP calls this only when the last packet reference has gone. */
static void release (struct pbuf * p_packet)
{
    /* The native pbuf is the first member of the custom header. */
    struct pbuf_custom * const p_header = (struct pbuf_custom *)p_packet;
    const uint32_t             saved    = c3bport_enter();
    p_header->custom_free_function      = NULL;
    c3bport_leave(saved);
}

#endif

/* End of file. */

