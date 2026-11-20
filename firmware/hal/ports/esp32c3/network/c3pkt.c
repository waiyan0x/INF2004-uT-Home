/**
 * @file c3pkt.c
 * @brief Own TCP packets and the ARP frame until their last lwIP reference.
 */
#include "c3pkt.h"
#if UTHOME_NETWORK_WIFI && UTHOME_STATIC_NET_QUEUES
#include "c3bport.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/prot/etharp.h"
#include "lwip/prot/ethernet.h"
#include "lwip/prot/udp.h"
#include <stdbool.h>
#include <stddef.h>

#if !LWIP_SUPPORT_CUSTOM_PBUF || !LWIP_NETIF_TX_SINGLE_PBUF ||                 \
    (MEM_ALIGNMENT != 4)
#error                                                                         \
    "Fixed TCP packets require the reviewed custom, single-buffer Wi-Fi port."
#endif

/* IPv4 MTU set by the pinned station adapter, excluding Ethernet header. */
#define C3PKT_MTU           1500U
#define C3PKT_DATA_SLOTS    5U
#define C3PKT_CONTROL_SLOTS 2U
#define C3PKT_CONTROL_BYTES (TCP_HLEN + TCP_MAX_OPTION_BYTES + 1U)

typedef struct
{
    struct pbuf_custom header;
    uint32_t words[LWIP_MEM_ALIGN_SIZE(LWIP_MEM_ALIGN_SIZE(PBUF_TRANSPORT) +
                                       TCP_MSS) /
                   sizeof(uint32_t)];
} c3pkt_data_t;

typedef struct
{
    struct pbuf_custom header;
    uint32_t           words[LWIP_MEM_ALIGN_SIZE(LWIP_MEM_ALIGN_SIZE(PBUF_IP) +
                                       C3PKT_CONTROL_BYTES) /
                   sizeof(uint32_t)];
} c3pkt_control_t;

typedef struct
{
    struct pbuf_custom header;
    uint32_t words[LWIP_MEM_ALIGN_SIZE(LWIP_MEM_ALIGN_SIZE(PBUF_LINK) +
                                       SIZEOF_ETHARP_HDR) /
                   sizeof(uint32_t)];
} c3pkt_arp_t;

typedef struct
{
    struct pbuf_custom header;
    uint32_t
        words[LWIP_MEM_ALIGN_SIZE(LWIP_MEM_ALIGN_SIZE(PBUF_LINK) + C3PKT_MTU) /
              sizeof(uint32_t)];
} c3pkt_frame_t;

typedef struct
{
    struct pbuf_custom header;
    uint32_t
        words[LWIP_MEM_ALIGN_SIZE(LWIP_MEM_ALIGN_SIZE(PBUF_IP) + UDP_HLEN) /
              sizeof(uint32_t)];
} c3pkt_udp_t;

static c3pkt_frame_t   g_frame                        = {0};
static c3pkt_udp_t     g_udp                          = {0};
static c3pkt_arp_t     g_arp                          = {0};
static c3pkt_data_t    g_data[C3PKT_DATA_SLOTS]       = {0};
static c3pkt_control_t g_control[C3PKT_CONTROL_SLOTS] = {0};
/* Only lwIP's serialized TCP core enters the split or allocates a packet. */
static bool gb_splitting = false;

static void release (struct pbuf * p_packet);

static struct pbuf * reserve (pbuf_layer           layer,
                              uint16_t             length,
                              struct pbuf_custom * p_header,
                              void *               p_storage,
                              uint16_t             size);

/** @brief Leave one data slot for a split and separate capacity for ACKs. */
struct pbuf * c3pkt_alloc (pbuf_layer layer, uint16_t length, pbuf_type type)
{
    struct pbuf_custom * p_header  = NULL;
    void *               p_storage = NULL;
    uint16_t             size      = 0U;

    if (PBUF_RAM != type)
    {
        return NULL;
    }
    const uint32_t saved = c3bport_enter();
    if ((PBUF_TRANSPORT == layer) && (TCP_MSS >= length))
    {
        size_t free_count = 0U;
        for (size_t index = 0U; index < C3PKT_DATA_SLOTS; ++index)
        {
            if (NULL == g_data[index].header.custom_free_function)
            {
                ++free_count;
                if (gb_splitting || (free_count > 1U))
                {
                    p_header  = &g_data[index].header;
                    p_storage = g_data[index].words;
                    size      = sizeof(g_data[index].words);
                    break;
                }
            }
        }
    }
    else if ((PBUF_IP == layer) && (C3PKT_CONTROL_BYTES >= length))
    {
        for (size_t index = 0U; index < C3PKT_CONTROL_SLOTS; ++index)
        {
            if (NULL == g_control[index].header.custom_free_function)
            {
                p_header  = &g_control[index].header;
                p_storage = g_control[index].words;
                size      = sizeof(g_control[index].words);
                break;
            }
        }
    }
    else if ((PBUF_LINK == layer) && (SIZEOF_ETHARP_HDR == length) &&
             (NULL == g_arp.header.custom_free_function))
    {
        p_header  = &g_arp.header;
        p_storage = g_arp.words;
        size      = sizeof(g_arp.words);
    }
    else
    {
        /* No storage for this frame shape or an already leased ARP frame. */
    }
    struct pbuf * const p_packet =
        reserve(layer, length, p_header, p_storage, size);
    c3bport_leave(saved);
    return p_packet;
}

/** @brief Select a UDP header or one contiguous station frame by native layer.
 */
struct pbuf * c3pkt_aux (pbuf_layer layer, uint16_t length, pbuf_type type)
{
    struct pbuf * p_packet = NULL;
    if (PBUF_RAM == type)
    {
        const uint32_t saved = c3bport_enter();
        if ((PBUF_IP == layer) && (UDP_HLEN == length))
        {
            p_packet = reserve(
                layer, length, &g_udp.header, g_udp.words, sizeof(g_udp.words));
        }
        else if (((PBUF_LINK == layer) && (C3PKT_MTU >= length)) ||
                 ((PBUF_RAW_TX == layer) &&
                  ((C3PKT_MTU + SIZEOF_ETH_HDR) >= length)))
        {
            p_packet = reserve(layer,
                               length,
                               &g_frame.header,
                               g_frame.words,
                               sizeof(g_frame.words));
        }
        else
        {
            /* Outside the station's reviewed packet shapes. */
        }
        c3bport_leave(saved);
    }
    return p_packet;
}

/** @brief Copy the entire volatile chain while its caller owns the source. */
struct pbuf *
c3pkt_clone (pbuf_layer layer, pbuf_type type, struct pbuf * p_source)
{
    struct pbuf * p_copy = NULL;
    if ((NULL != p_source) && (PBUF_LINK == layer))
    {
        p_copy = c3pkt_aux(layer, p_source->tot_len, type);
        if ((NULL != p_copy) && (ERR_OK != pbuf_copy(p_copy, p_source)))
        {
            (void)pbuf_free(p_copy);
            p_copy = NULL;
        }
    }
    return p_copy;
}

/** @brief Caller holds the interrupt guard while leasing aligned slot storage.
 */
static struct pbuf * reserve (pbuf_layer           layer,
                              uint16_t             length,
                              struct pbuf_custom * p_header,
                              void *               p_storage,
                              uint16_t             size)
{
    struct pbuf * p_packet = NULL;
    if ((NULL != p_header) && (NULL == p_header->custom_free_function))
    {
        p_packet = pbuf_alloced_custom(
            layer, length, PBUF_RAM, p_header, p_storage, size);
        if (NULL != p_packet)
        {
            p_header->custom_free_function = release;
        }
    }
    return p_packet;
}

/** @brief Permit a fifth transport packet only inside the native TCP split. */
err_t c3pkt_split (struct tcp_pcb * p_connection, uint16_t length)
{
    gb_splitting       = true;
    const err_t status = tcp_split_unsent_seg(p_connection, length);
    gb_splitting       = false;
    return status;
}

/** @brief A custom header begins with the exact pbuf supplied by lwIP. */
static void release (struct pbuf * p_packet)
{
    struct pbuf_custom * const p_header = (struct pbuf_custom *)p_packet;
    const uint32_t             saved    = c3bport_enter();
    p_header->custom_free_function      = NULL;
    c3bport_leave(saved);
}

#endif

/* End of file. */

